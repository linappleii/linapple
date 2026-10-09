#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0-only
#
# Writes the generated disk-image fixtures into the directory named on the
# command line (default: this script's own directory). Every byte is derived
# here, so the SHA1SUMS beside this file pin the fixtures and a regeneration
# into a scratch directory can prove nothing drifted.
#
# minimal.dsk, minimal.woz and minimal-v1.woz are hand-made and committed;
# this script reads the first and last as payloads but never rewrites them.
use strict;
use warnings;

use Compress::Zlib qw(crc32);
use File::Basename qw(dirname);
use IO::Compress::Gzip qw($GzipError);
use IO::Compress::Zip qw($ZipError ZIP_CM_STORE);

# Zip local headers carry DOS date-times, which perl converts from the epoch
# through the local zone; pinning the zone keeps the archives byte-identical
# on every machine.
$ENV{TZ} = 'UTC';

my $script_dir = dirname($0);
my $out_dir    = $ARGV[0] // $script_dir;
-d $out_dir or die "$out_dir is not a directory\n";

use constant {
    SECTOR_SIZE         => 256,
    SECTORS_PER_TRACK   => 16,
    TRACKS              => 35,
    TRACK_SIZE          => 4096,
    DSK_SIZE            => 143_360,
    BLOCK_SIZE          => 512,
    NIB_TRACK_SIZE      => 6656,
    NB2_TRACK_SIZE      => 6384,
    IIE_HEADER_SIZE     => 88,
    IIE_SECTOR_DATA_AT  => 30,
    MACBINARY_HEADER    => 128,
    WOZ_INFO_SIZE       => 60,
    WOZ_TMAP_ENTRIES    => 160,
    WOZ2_TRKS_ENTRY     => 8,
    ZIP_EPOCH_1980      => 315_532_800,
    HDV_BLOCKS          => 16,
    TWO_IMG_HEADER_SIZE => 64,
};

sub slurp {
    my ($path) = @_;
    open my $fh, '<:raw', $path or die "cannot read $path: $!\n";
    local $/;
    my $data = <$fh>;
    close $fh;
    return $data;
}

sub emit {
    my ( $name, $data ) = @_;
    my $path = "$out_dir/$name";
    open my $fh, '>:raw', $path or die "cannot write $path: $!\n";
    print {$fh} $data or die "cannot write $path: $!\n";
    close $fh or die "cannot close $path: $!\n";
    return;
}

sub patch {
    my ( $image, $offset, $bytes ) = @_;
    substr( $image, $offset, length $bytes, $bytes );
    return $image;
}

my $dsk    = slurp("$script_dir/minimal.dsk");
my $v1_woz = slurp("$script_dir/minimal-v1.woz");
length $dsk == DSK_SIZE or die "minimal.dsk is not a 140K image\n";

# ---------------------------------------------------------------------------
# ProDOS-order image: an empty volume whose directory chain and bitmap are
# where the ProDOS Technical Reference puts them, with every data sector
# marked by its file position so the two interleaves read differently.
# ---------------------------------------------------------------------------
sub prodos_image {
    my $image = "\0" x DSK_SIZE;

    my $volume_header = pack(
        'v v C a15 x8 x4 C C C C C v v v',
        0, 3, 0xF7, 'MINIMAL', 0, 0, 0xC3, 0x27, 0x0D, 0, 6, 280
    );
    $image = patch( $image, 2 * BLOCK_SIZE, $volume_header );
    for my $block ( 3 .. 5 ) {
        my $next = $block == 5 ? 0 : $block + 1;
        $image = patch( $image, $block * BLOCK_SIZE, pack( 'v v', $block - 1, $next ) );
    }

    # A set bit is a free block; blocks 0-6 hold the boot loader, the
    # directory and this bitmap, and 280 blocks fill exactly 35 bytes.
    my $bitmap = chr(0x01) . ( "\xFF" x 34 );
    $image = patch( $image, 6 * BLOCK_SIZE, $bitmap );

    for my $track ( 1 .. TRACKS - 1 ) {
        for my $sector ( 0 .. SECTORS_PER_TRACK - 1 ) {
            my $marker = chr( ( $track * SECTORS_PER_TRACK + $sector ) & 0xFF );
            $image = patch( $image, $track * TRACK_SIZE + $sector * SECTOR_SIZE,
                $marker x SECTOR_SIZE );
        }
    }
    return $image;
}

# ---------------------------------------------------------------------------
# DOS-order serialization of a ProDOS volume. ProDOS block b of a 5.25-inch
# disk is the pair of DOS 3.3 sectors Fig. 3.14 of Beneath Apple ProDOS (pp.
# 3-16 to 3-18) gives for b & 7, first-named sector first, on track b >> 3;
# a .dsk file holds DOS sector s of track t at (t * 16 + s) * 256.
# ---------------------------------------------------------------------------
my @prodos_block_sectors = (
    [ 0x0, 0xE ], [ 0xD, 0xC ], [ 0xB, 0xA ], [ 0x9, 0x8 ],
    [ 0x7, 0x6 ], [ 0x5, 0x4 ], [ 0x3, 0x2 ], [ 0x1, 0xF ]
);

sub dos_order_image {
    my ($prodos) = @_;
    length $prodos == DSK_SIZE or die "a DOS-order image needs a 140K volume\n";
    my $image = "\0" x DSK_SIZE;
    for my $block ( 0 .. DSK_SIZE / BLOCK_SIZE - 1 ) {
        my $track = $block >> 3;
        my ( $first, $second ) = @{ $prodos_block_sectors[ $block & 7 ] };
        $image = patch( $image, $track * TRACK_SIZE + $first * SECTOR_SIZE,
            substr( $prodos, $block * BLOCK_SIZE, SECTOR_SIZE ) );
        $image = patch( $image, $track * TRACK_SIZE + $second * SECTOR_SIZE,
            substr( $prodos, $block * BLOCK_SIZE + SECTOR_SIZE, SECTOR_SIZE ) );
    }
    return $image;
}

# ---------------------------------------------------------------------------
# An empty DOS 3.3 volume (Beneath Apple DOS ch. 4): the VTOC at track 17
# sector 0 names the first catalog sector, the volume, the track and sector
# counts and the sector size; the catalog runs from sector 15 down to sector
# 1, each linking the one below it, and the last link is 00 00.
# ---------------------------------------------------------------------------
sub dos33_image {
    my $image = "\0" x DSK_SIZE;
    my $vtoc  = "\0" x SECTOR_SIZE;
    $vtoc = patch( $vtoc, 1,    pack( 'C C C', 0x11, 0x0F, 0x03 ) );
    $vtoc = patch( $vtoc, 6,    chr(0xFE) );
    $vtoc = patch( $vtoc, 0x27, chr(0x7A) );
    $vtoc = patch( $vtoc, 0x34, pack( 'C C v', TRACKS, SECTORS_PER_TRACK, SECTOR_SIZE ) );
    $image = patch( $image, 17 * TRACK_SIZE, $vtoc );
    for my $sector ( 1 .. 15 ) {
        my $link = $sector > 1 ? pack( 'C C', 0x11, $sector - 1 ) : "\0\0";
        $image = patch( $image, 17 * TRACK_SIZE + $sector * SECTOR_SIZE + 1, $link );
    }
    return $image;
}

# ---------------------------------------------------------------------------
# A 16-block ProDOS-order hard disk image. Block 0 begins 01 4C 01 08: a boot
# block whose first instruction jumps to itself, so a boot through a card is
# seen to reach $0801 and stay there. Blocks 1-15 are filled with their own
# number, so a misaddressed read shows in its first byte.
# ---------------------------------------------------------------------------
sub block_image {
    my $image = pack( 'C4', 0x01, 0x4C, 0x01, 0x08 ) . ( "\0" x ( BLOCK_SIZE - 4 ) );
    $image .= chr($_) x BLOCK_SIZE for 1 .. HDV_BLOCKS - 1;
    return $image;
}

# ---------------------------------------------------------------------------
# 2IMG header (the 2IMG specification): magic, creator, header size, version,
# image format (0 DOS 3.3, 1 ProDOS, 2 nibble), flags (bit 31 locked), block
# count, data offset and length, comment offset and length, creator-data
# offset and length, sixteen reserved bytes. Every field is little-endian.
# ---------------------------------------------------------------------------
sub two_img_header {
    my (%field) = @_;
    my $header = pack(
        'a4 a4 v v V V V V V V V V V x16',
        '2IMG', 'LinA',
        $field{header_size}    // TWO_IMG_HEADER_SIZE,
        $field{version}        // 1,
        $field{format}         // 1,
        $field{flags}          // 0,
        $field{blocks}         // 0,
        $field{data_offset}    // TWO_IMG_HEADER_SIZE,
        $field{data_length}    // 0,
        $field{comment_offset} // 0,
        $field{comment_length} // 0,
        0, 0
    );
    length $header == TWO_IMG_HEADER_SIZE or die "2IMG header is not 64 bytes\n";
    return $header;
}

sub two_img_image {
    my ( $payload, %field ) = @_;
    return two_img_header( blocks => length($payload) / BLOCK_SIZE, data_length => length $payload, %field ) . $payload;
}

# ---------------------------------------------------------------------------
# One formatted track of zero sectors, as the tree's own encoder lays it out:
# DOS 3.3 field structure with a 48-nibble lead-in gap, a six-nibble gap
# between address and data fields and sixteen after each sector. A zero
# sector 6-and-2 encodes to 343 copies of the alphabet's first symbol, so no
# encoder is needed to reproduce it.
# ---------------------------------------------------------------------------
sub four_and_four {
    my ($byte) = @_;
    return chr( ( ( $byte >> 1 ) & 0x55 ) | 0xAA ) . chr( ( $byte & 0x55 ) | 0xAA );
}

sub formatted_track {
    my ($track) = @_;
    my $volume  = 0xFE;
    my $nibbles = "\xFF" x 48;
    for my $sector ( 0 .. SECTORS_PER_TRACK - 1 ) {
        $nibbles .= "\xD5\xAA\x96";
        $nibbles .= four_and_four($_) for $volume, $track, $sector, $volume ^ $track ^ $sector;
        $nibbles .= "\xDE\xAA\xEB";
        $nibbles .= "\xFF" x 6;
        $nibbles .= "\xD5\xAA\xAD";
        $nibbles .= "\x96" x 343;
        $nibbles .= "\xDE\xAA\xEB";
        $nibbles .= "\xFF" x 16;
    }
    length $nibbles == 6208 or die "formatted track is not 6208 nibbles\n";
    return $nibbles;
}

sub nibble_image {
    my ($slot_size) = @_;
    my $image = '';
    for my $track ( 0 .. TRACKS - 1 ) {
        my $nibbles = formatted_track($track);
        $image .= $nibbles . ( "\xFF" x ( $slot_size - length $nibbles ) );
    }
    return $image;
}

# ---------------------------------------------------------------------------
# SimSystem //e images. The legacy variant's header map names, for each file
# sector, the physical sector it sits in; the DOS 3.3 logical-to-physical
# table is the natural permutation. The nibble variant's per-track counts are
# deliberately unequal so a reader that mis-accumulates them lands off a track.
# ---------------------------------------------------------------------------
my @dos_logical_to_physical = (
    0x00, 0x0D, 0x0B, 0x09, 0x07, 0x05, 0x03, 0x01,
    0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x0F
);

sub iie_legacy_image {
    my $header = 'SIMSYSTEM_IIE' . chr(0) . pack( 'C16', @dos_logical_to_physical );
    length $header == IIE_SECTOR_DATA_AT or die "legacy .iie header is not 30 bytes\n";
    my $sectors = '';
    for my $track ( 0 .. TRACKS - 1 ) {
        for my $sector ( 0 .. SECTORS_PER_TRACK - 1 ) {
            $sectors .= chr( ( $track * SECTORS_PER_TRACK + $sector ) & 0xFF ) x SECTOR_SIZE;
        }
    }
    return $header . $sectors;
}

sub iie_nibble_image {
    my @counts = map { ( 6208, 6656, 100 )[ $_ % 3 ] } 0 .. TRACKS - 1;
    my $header = 'SIMSYSTEM_IIE' . chr(3) . pack( 'v*', @counts );
    $header .= "\0" x ( IIE_HEADER_SIZE - length $header );
    my $data = '';
    for my $track ( 0 .. TRACKS - 1 ) {
        my $slot = formatted_track($track);
        $slot .= "\xFF" x ( NIB_TRACK_SIZE - length $slot );
        $data .= substr( $slot, 0, $counts[$track] );
    }
    return $header . $data;
}

# ---------------------------------------------------------------------------
# Containers. Zip entries are stored rather than deflated because perl links
# whatever zlib the system ships and deflate output differs between
# implementations; a stored archive is byte-identical everywhere.
# ---------------------------------------------------------------------------
sub gzip_bytes {
    my ($payload) = @_;
    my $out;
    IO::Compress::Gzip::gzip( \$payload, \$out, Minimal => 1 ) or die "gzip failed: $GzipError\n";
    return $out;
}

sub zip_bytes {
    my (@entries) = @_;
    my $out;
    my $archive = IO::Compress::Zip->new(
        \$out,
        Name    => $entries[0][0],
        Time    => ZIP_EPOCH_1980,
        Minimal => 1,
        Method  => ZIP_CM_STORE,
    ) or die "zip failed: $ZipError\n";
    for my $index ( 0 .. $#entries ) {
        my ( $name, $data ) = @{ $entries[$index] };
        if ( $index > 0 ) {
            $archive->newStream(
                Name    => $name,
                Time    => ZIP_EPOCH_1980,
                Minimal => 1,
                Method  => ZIP_CM_STORE,
            ) or die "zip failed: $ZipError\n";
        }
        $archive->print($data) if length $data;
    }
    $archive->close or die "zip failed: $ZipError\n";
    return $out;
}

# The resource-fork sidecar macOS archivers add: an AppleDouble header with
# no entries, whose 00 05 magic is what a MacBinary sniffer must not mistake
# for a wrapper.
sub appledouble_stub {
    return pack( 'N N x16 n', 0x00051607, 0x00020000, 0 );
}

sub crc16_xmodem {
    my ($data) = @_;
    my $crc = 0;
    for my $byte ( unpack 'C*', $data ) {
        $crc ^= $byte << 8;
        for ( 1 .. 8 ) {
            $crc = ( $crc & 0x8000 ) ? ( ( $crc << 1 ) ^ 0x1021 ) : ( $crc << 1 );
            $crc &= 0xFFFF;
        }
    }
    return $crc;
}

sub macbinary_wrap {
    my ( $name, $payload ) = @_;
    my $header = "\0" x MACBINARY_HEADER;
    $header = patch( $header, 1,   chr( length $name ) . $name );
    $header = patch( $header, 83,  pack( 'N', length $payload ) );
    $header = patch( $header, 122, "\x81\x81" );
    $header = patch( $header, 124, pack( 'n', crc16_xmodem( substr $header, 0, 124 ) ) );
    my $padding = ( -length $payload ) % MACBINARY_HEADER;
    return $header . $payload . ( "\0" x $padding );
}

# ---------------------------------------------------------------------------
# WOZ 2.x images. INFO is padded to its fixed 60 bytes; TMAP has one byte per
# quarter track; TRKS is 160 eight-byte entries followed by the track data,
# which the entries address in 512-byte blocks counted from the file start.
# ---------------------------------------------------------------------------
sub woz_chunk {
    my ( $id, $data ) = @_;
    return $id . pack( 'V', length $data ) . $data;
}

sub woz2_info {
    my (%field) = @_;
    my $info = pack(
        'C C C C C A32 C C C v v v v v',
        $field{version}, 1, 0, 0, 0, 'linapple fixture', 1, 1, 32, 0, 0,
        $field{largest_track}, $field{flux_block} // 0, $field{largest_flux_track} // 0
    );
    return $info . ( "\0" x ( WOZ_INFO_SIZE - length $info ) );
}

sub woz2_map {
    my (%entry) = @_;
    my $map = "\xFF" x WOZ_TMAP_ENTRIES;
    $map = patch( $map, $_, chr( $entry{$_} ) ) for keys %entry;
    return $map;
}

sub woz2_trks {
    my ( $entries, $track_data ) = @_;
    my $table = join '', map { pack( 'v v V', @{$_} ) } @{$entries};
    $table .= "\0" x ( WOZ_TMAP_ENTRIES * WOZ2_TRKS_ENTRY - length $table );
    return $table . $track_data;
}

sub woz2_file {
    my (@chunks) = @_;
    return "WOZ2\xFF\n\r\n" . pack( 'V', 0 ) . join( '', @chunks );
}

sub woz_with_crc {
    my ($image) = @_;
    return patch( $image, 8, pack( 'V', crc32( substr $image, 12 ) ) );
}

my $track_pattern = join '', map { chr( ( 7 * $_ + 1 ) & 0xFF ) } 0 .. BLOCK_SIZE - 1;

my $track_woz = woz2_file(
    woz_chunk( 'INFO', woz2_info( version => 2, largest_track => 1 ) ),
    woz_chunk( 'TMAP', woz2_map( 0 => 0 ) ),
    woz_chunk( 'TRKS', woz2_trks( [ [ 3, 1, 4096 ] ], $track_pattern ) ),
);
length $track_woz == 2048 or die "minimal-track.woz is not 2048 bytes\n";

# A 2.1 flux-only track: TMAP leaves quarter track 0 unrecorded and the FLUX
# map, which INFO locates by block, names the TRKS entry holding flux timing
# bytes in place of cells. The FLUX chunk must begin on a block boundary.
my $flux_data = "\x20" x BLOCK_SIZE;
my $flux_woz  = woz2_file(
    woz_chunk( 'INFO', woz2_info( version => 3, largest_track => 0, flux_block => 4, largest_flux_track => 1 ) ),
    woz_chunk( 'TMAP', woz2_map() ),
    woz_chunk( 'TRKS', woz2_trks( [ [ 3, 1, BLOCK_SIZE ] ], $flux_data ) ),
    woz_chunk( 'FLUX', woz2_map( 0 => 0 ) ),
);
index( $flux_woz, 'FLUX' ) == 4 * BLOCK_SIZE or die "FLUX chunk is off its block\n";

# ---------------------------------------------------------------------------

my $false_positive = patch( patch( $dsk, 0, "\x00\x05" ), 122, "\0" );

my $prodos    = prodos_image();
my $hdv       = block_image();
my $two_img   = two_img_image($hdv);
my $comment   = 'comment ' x ( BLOCK_SIZE / 8 );
my $nibble_ish = "\x96" x length $hdv;

my %fixture = (
    'minimal.po'               => $prodos,
    'minimal-prodos.dsk'       => dos_order_image($prodos),
    'minimal-dos33.dsk'        => dos33_image(),
    'minimal-block.hdv'        => $hdv,
    'minimal-block.hdv.gz'     => gzip_bytes($hdv),
    'minimal-block.hdv.zip'    => zip_bytes( [ 'minimal-block.hdv', $hdv ] ),
    'minimal-block.2mg'        => $two_img,
    'minimal-block-locked.2mg' => two_img_image( $hdv, flags => 0x8000_0000 ),
    'minimal-block-comment.2mg' => two_img_image( $hdv, comment_offset => TWO_IMG_HEADER_SIZE + length $hdv, comment_length => length $comment ) . $comment,
    'minimal-block-format0.2mg' => two_img_image( dos_order_image($prodos), format => 0 ),
    'minimal-block-format2.2mg' => two_img_image( $nibble_ish, format => 2 ),
    'minimal-block-short-header.2mg' => two_img_image( $hdv, header_size => 32 ),
    'minimal-block-oversize.2mg' => two_img_image( $hdv, blocks => 8, data_length => 8 * BLOCK_SIZE ),
    'minimal-block-disagree.2mg' => two_img_image( $hdv, blocks => 8 ),
    'minimal-block-nomagic.2mg' => patch( $two_img, 0, 'X' ),
    'minimal-macbinary.2mg'    => macbinary_wrap( 'minimal-block.2mg', $two_img ),
    'minimal.nib'              => nibble_image(NIB_TRACK_SIZE),
    'minimal.nb2'              => nibble_image(NB2_TRACK_SIZE),
    'minimal-legacy.iie'       => iie_legacy_image(),
    'minimal-nibble.iie'       => iie_nibble_image(),
    'minimal.dsk.gz'           => gzip_bytes($dsk),
    'minimal.dsk.zip'          => zip_bytes( [ 'minimal.dsk', $dsk ] ),
    'minimal-dir-first.zip'    => zip_bytes( [ 'folder/', '' ], [ 'folder/minimal.dsk', $dsk ] ),
    'minimal-macosx.zip'       => zip_bytes( [ '__MACOSX/._minimal.dsk', appledouble_stub() ], [ 'minimal.dsk', $dsk ] ),
    'minimal-macbinary.dsk'    => macbinary_wrap( 'minimal.dsk', $dsk ),
    'minimal-macbinary.woz'    => macbinary_wrap( 'minimal-track.woz', $track_woz ),
    'minimal-macbinary-v1.woz' => macbinary_wrap( 'minimal-v1.woz', $v1_woz ),
    'minimal-falsepositive.dsk' => $false_positive,
    'minimal-track.woz'        => $track_woz,
    'minimal-crc.woz'          => woz_with_crc($track_woz),
    'woz21-flux.woz'           => $flux_woz,
);

emit( $_, $fixture{$_} ) for sort keys %fixture;
