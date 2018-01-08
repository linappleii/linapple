/*
  Hatari - Shared.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
  M. SARO 2013
*/

#import <Cocoa/Cocoa.h>

// add some macro for easy writing

#define localize(laklef) [[NSBundle mainBundle] localizedStringForKey:laklef value:(laklef != nil ? laklef : @"???") table:@"Localizable"]

// disk extensions allowed in open box
#define allF	@"st",@"msa",@"dim",@"gz",@"zip",@"stx",@"ipf",@"raw",@"ctr"
// cartridge extensions
#define allC	@"img",@"rom",@"bin",@"cart"
// TOS extensions
#define allT	@"img",@"rom",@"bin"



// Wrapper to run an NSWindow modally
@protocol NSWindowDelegate;

@interface ModalWrapper : NSWindowController <NSWindowDelegate>
{
    IBOutlet NSWindow *modalWindow;
}
- (void)runModal:(NSWindow*)window;
- (void)windowWillClose:(NSNotification*)notification;
@end

// Helper function to write the contents of a path as an NSString to a string
void GuiOsx_ExportPathString(NSString* path, char* szTarget, size_t cchTarget);

// Pauses emulation and gets ready to use Cocoa UI
void GuiOsx_Pause(void);

// Switches back to emulation mode and resume emulation
void GuiOsx_Resume(void);


// Add method for general Usage  
//
@interface NSApplication (service)

// Some useful tools
// choose file to open
- (NSString *)hopenfile:(BOOL)chooseDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types ;
- (NSString *)hopenfile:(BOOL)chooseDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types titre:(NSString *)titre ;

// choose file to save
- (NSString *)hsavefile:(BOOL)creatDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types ;
- (NSString *)hsavefile:(BOOL)creatDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types titre:(NSString *)titre ;

// Return localized path, Full path or partial path.
- (NSString *)localpath:(NSString *)thepath ;		// Full
- (NSString *)pathUser:(NSString *)thepath ;		// Partial if possible.

// Alert available 10.4 to 10.9 (styles: NSWarningAlertStyle, NSInformationalAlertStyle, NSCriticalAlertStyle)
//  return:   NSAlertDefaultReturn, NSAlertAlternateReturn, and NSAlertOtherReturn.
- (NSInteger)myAlerte:(NSUInteger)style Txt:(NSString *)Txt firstB:(NSString *)firstB alternateB:(NSString *)alternateB
										otherB:(NSString *)otherB informativeTxt:(NSString *)informativeT ;

@end
