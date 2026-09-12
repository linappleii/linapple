// SPDX-License-Identifier: GPL-2.0-only
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/config/Toml.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("TOML: Document Creation and Table Access") {
  auto doc = toml_document_create();
  REQUIRE(doc != nullptr);

  // Table that doesn't exist yet
  CHECK(toml_find_table(doc.get(), "Core") == nullptr);

  // Create table
  auto* core_table = toml_get_or_create_table(doc.get(), "Core");
  REQUIRE(core_table != nullptr);
  CHECK(toml_find_table(doc.get(), "Core") == core_table);

  // Add values
  toml_table_set_string(core_table, "Machine", "Apple //e Enhanced",
                        "Default model");
  toml_table_set_int(core_table, "SlotCount", 7);
  toml_table_set_double(core_table, "Speed", 1.0);
  toml_table_set_bool(core_table, "Debugger", true);

  CHECK(toml_table_has_key(core_table, "Machine") == true);
  CHECK(toml_table_has_key(core_table, "NonExistent") == false);

  CHECK(toml_table_get_string(core_table, "Machine") == "Apple //e Enhanced");
  CHECK(toml_table_get_int(core_table, "SlotCount") == 7);
  CHECK(toml_table_get_double(core_table, "Speed") == doctest::Approx(1.0));
  CHECK(toml_table_get_bool(core_table, "Debugger") == true);

  // Default value fallbacks
  CHECK(toml_table_get_string(core_table, "MissingStr", "fallback") ==
        "fallback");
  CHECK(toml_table_get_int(core_table, "MissingInt", 42) == 42);
  CHECK(toml_table_get_double(core_table, "MissingDbl", 3.14) ==
        doctest::Approx(3.14));
  CHECK(toml_table_get_bool(core_table, "MissingBool", false) == false);
}

TEST_CASE("TOML: Defensive Null Checks") {
  CHECK(toml_find_table(nullptr, "Core") == nullptr);
  CHECK(toml_get_or_create_table(nullptr, "Core") == nullptr);
  CHECK(toml_table_has_key(nullptr, "key") == false);
  CHECK(toml_table_get_value(nullptr, "key") == nullptr);
  CHECK(toml_table_get_string(nullptr, "key", "def") == "def");
  CHECK(toml_table_get_int(nullptr, "key", 99) == 99);
  CHECK(toml_table_get_double(nullptr, "key", 1.23) == doctest::Approx(1.23));
  CHECK(toml_table_get_bool(nullptr, "key", true) == true);
  CHECK(toml_table_get_array(nullptr, "key") == nullptr);

  // Safe no-ops
  toml_table_set_string(nullptr, "k", "v");
  toml_table_set_int(nullptr, "k", 1);
  toml_table_set_double(nullptr, "k", 1.0);
  toml_table_set_bool(nullptr, "k", true);
  toml_table_set_array(nullptr, "k", {});

  CHECK(toml_document_serialize(nullptr).empty());
  std::string err;
  CHECK(toml_document_save_file(nullptr, "/tmp/null.toml", &err) == false);
}

TEST_CASE("TOML: Parse Full linapple.toml Sample") {
  const std::string toml_sample = R"(
# LinApple Configuration
[Core]
Machine = Apple //e Enhanced
EmulationSpeed = 1.0x # 1.023 MHz
BootOnStartup = false
EnableDebugger = true
BasicLineNumbering = "Explicit"

[Video]
VideoStandard = NTSC
VideoEmulation = "Color Standard"
MonochromeColor = #C0C0C0

[Frontend]
ScreenFactor = 1.5
Fullscreen = false
Multithreaded = true
ShowLeds = true
RenderMode = Smart

[Audio]
SpeakerVolume = 15

[Slots]
Slot1 = ParallelPrinter
Slot2 = SuperSerial
Slot4 = Mockingboard
Slot6 = DiskII
Slot7 = Harddisk

[Peripheral.DiskII.Slot6]
Drive1 = "/path/to/disk1.dsk"
Drive2 = ""

[Keyboard.Custom]
W = "Up"
A = "Left"
S = "Down"
D = "Right"
)";

  std::string parse_err;
  auto doc = toml_document_parse(toml_sample, &parse_err);
  REQUIRE(doc != nullptr);
  CHECK(parse_err.empty());

  const auto* core = toml_find_table(doc.get(), "Core");
  REQUIRE(core != nullptr);
  CHECK(toml_table_get_string(core, "Machine") == "Apple //e Enhanced");
  CHECK(toml_table_get_double(core, "EmulationSpeed") == doctest::Approx(1.0));
  CHECK(toml_table_get_bool(core, "BootOnStartup") == false);
  CHECK(toml_table_get_bool(core, "EnableDebugger") == true);
  CHECK(toml_table_get_string(core, "BasicLineNumbering") == "Explicit");

  const auto* video = toml_find_table(doc.get(), "Video");
  REQUIRE(video != nullptr);
  CHECK(toml_table_get_string(video, "VideoStandard") == "NTSC");
  CHECK(toml_table_get_string(video, "VideoEmulation") == "Color Standard");
  CHECK(toml_table_get_string(video, "MonochromeColor") == "#C0C0C0");

  const auto* frontend = toml_find_table(doc.get(), "Frontend");
  REQUIRE(frontend != nullptr);
  CHECK(toml_table_get_double(frontend, "ScreenFactor") ==
        doctest::Approx(1.5));
  CHECK(toml_table_get_bool(frontend, "Fullscreen") == false);
  CHECK(toml_table_get_bool(frontend, "Multithreaded") == true);
  CHECK(toml_table_get_bool(frontend, "ShowLeds") == true);
  CHECK(toml_table_get_string(frontend, "RenderMode") == "Smart");

  const auto* audio = toml_find_table(doc.get(), "Audio");
  REQUIRE(audio != nullptr);
  CHECK(toml_table_get_int(audio, "SpeakerVolume") == 15);

  const auto* slots = toml_find_table(doc.get(), "Slots");
  REQUIRE(slots != nullptr);
  CHECK(toml_table_get_string(slots, "Slot1") == "ParallelPrinter");
  CHECK(toml_table_get_string(slots, "Slot4") == "Mockingboard");
  CHECK(toml_table_get_string(slots, "Slot6") == "DiskII");
  CHECK(toml_table_get_string(slots, "Slot7") == "Harddisk");

  const auto* slot6 = toml_find_table(doc.get(), "Peripheral.DiskII.Slot6");
  REQUIRE(slot6 != nullptr);
  CHECK(toml_table_get_string(slot6, "Drive1") == "/path/to/disk1.dsk");
  CHECK(toml_table_get_string(slot6, "Drive2").empty());

  const auto* kbd = toml_find_table(doc.get(), "Keyboard.Custom");
  REQUIRE(kbd != nullptr);
  CHECK(toml_table_get_string(kbd, "W") == "Up");
  CHECK(toml_table_get_string(kbd, "A") == "Left");
  CHECK(toml_table_get_string(kbd, "S") == "Down");
  CHECK(toml_table_get_string(kbd, "D") == "Right");
}

TEST_CASE("TOML: Number Formats and Hex Support") {
  const std::string content = R"(
[HexAndNumbers]
DecInt = 1234
NegativeInt = -5678
Hex0x = 0xC000
HexAlt = $C010
FloatVal = 3.14159
SpeedFactor = 2.5x
)";

  std::string err;
  auto doc = toml_document_parse(content, &err);
  REQUIRE(doc != nullptr);

  const auto* table = toml_find_table(doc.get(), "HexAndNumbers");
  REQUIRE(table != nullptr);

  CHECK(toml_table_get_int(table, "DecInt") == 1234);
  CHECK(toml_table_get_int(table, "NegativeInt") == -5678);
  CHECK(toml_table_get_int(table, "Hex0x") == 0xC000);
  CHECK(toml_table_get_int(table, "HexAlt") == 0xC010);
  CHECK(toml_table_get_double(table, "FloatVal") == doctest::Approx(3.14159));
  CHECK(toml_table_get_double(table, "SpeedFactor") == doctest::Approx(2.5));
}

TEST_CASE("TOML: Arrays and Lists") {
  const std::string content = R"(
[Arrays]
StringList = ["Apple", "Banana", "Cherry"]
NumList = [1, 2, 3, 4]
MixedQuotes = ['first', "second"]
)";

  std::string err;
  auto doc = toml_document_parse(content, &err);
  REQUIRE(doc != nullptr);

  const auto* table = toml_find_table(doc.get(), "Arrays");
  REQUIRE(table != nullptr);

  const auto* str_array = toml_table_get_array(table, "StringList");
  REQUIRE(str_array != nullptr);
  REQUIRE(str_array->size() == 3);
  CHECK((*str_array)[0].string_val == "Apple");
  CHECK((*str_array)[1].string_val == "Banana");
  CHECK((*str_array)[2].string_val == "Cherry");

  const auto* num_array = toml_table_get_array(table, "NumList");
  REQUIRE(num_array != nullptr);
  REQUIRE(num_array->size() == 4);
  CHECK((*num_array)[0].int_val == 1);
  CHECK((*num_array)[3].int_val == 4);
}

TEST_CASE("TOML: Serialization Round-Trip") {
  auto doc = toml_document_create();

  auto* core = toml_get_or_create_table(doc.get(), "Core");
  toml_table_set_string(core, "Machine", "Apple //e Enhanced");
  toml_table_set_double(core, "EmulationSpeed", 1.0);
  toml_table_set_bool(core, "BootOnStartup", false);

  auto* video = toml_get_or_create_table(doc.get(), "Video");
  toml_table_set_string(video, "VideoStandard", "NTSC");
  toml_table_set_string(video, "MonochromeColor", "#C0C0C0");

  const std::string serialized = toml_document_serialize(doc.get());
  CHECK_FALSE(serialized.empty());

  std::string parse_err;
  auto parsed_doc = toml_document_parse(serialized, &parse_err);
  REQUIRE(parsed_doc != nullptr);
  CHECK(parse_err.empty());

  const auto* parsed_core = toml_find_table(parsed_doc.get(), "Core");
  REQUIRE(parsed_core != nullptr);
  CHECK(toml_table_get_string(parsed_core, "Machine") == "Apple //e Enhanced");
  CHECK(toml_table_get_double(parsed_core, "EmulationSpeed") ==
        doctest::Approx(1.0));
  CHECK(toml_table_get_bool(parsed_core, "BootOnStartup") == false);

  const auto* parsed_video = toml_find_table(parsed_doc.get(), "Video");
  REQUIRE(parsed_video != nullptr);
  CHECK(toml_table_get_string(parsed_video, "VideoStandard") == "NTSC");
  CHECK(toml_table_get_string(parsed_video, "MonochromeColor") == "#C0C0C0");
}

TEST_CASE("TOML: Syntax Errors") {
  std::string err;

  // Missing '='
  CHECK(toml_document_parse("InvalidLineNoEquals", &err) == nullptr);
  CHECK(err.find("Expected '='") != std::string::npos);

  // Empty section header
  CHECK(toml_document_parse("[]", &err) == nullptr);
  CHECK(err.find("Empty section header") != std::string::npos);

  // Missing key
  CHECK(toml_document_parse("= 123", &err) == nullptr);
  CHECK(err.find("Missing key") != std::string::npos);
}

TEST_CASE("TOML: File Read/Write Operations") {
  const TestFixtures::ScopedTempFile_t temp_file(".toml");

  auto doc = toml_document_create();
  auto* table = toml_get_or_create_table(doc.get(), "TestSection");
  toml_table_set_string(table, "TestKey", "TestValue");
  toml_table_set_int(table, "Count", 42);

  std::string save_err;
  CHECK(toml_document_save_file(doc.get(), temp_file.path(), &save_err) ==
        true);
  CHECK(save_err.empty());

  std::string load_err;
  auto loaded_doc = toml_document_load_file(temp_file.path(), &load_err);
  REQUIRE(loaded_doc != nullptr);
  CHECK(load_err.empty());

  const auto* loaded_table = toml_find_table(loaded_doc.get(), "TestSection");
  REQUIRE(loaded_table != nullptr);
  CHECK(toml_table_get_string(loaded_table, "TestKey") == "TestValue");
  CHECK(toml_table_get_int(loaded_table, "Count") == 42);

  // Loading nonexistent file
  auto bad_doc =
      toml_document_load_file("/nonexistent/path/file.toml", &load_err);
  CHECK(bad_doc == nullptr);
  CHECK_FALSE(load_err.empty());
}

TEST_CASE("TOML: Edge Cases and Robustness") {
  SUBCASE("Hex color followed by inline comment") {
    const std::string content = "Color = #C0C0C0 # this is a gray color\n";
    std::string err;
    auto doc = toml_document_parse(content, &err);
    REQUIRE(doc != nullptr);
    const auto* root = toml_find_table(doc.get(), "");
    REQUIRE(root != nullptr);
    CHECK(toml_table_get_string(root, "Color") == "#C0C0C0");
    const auto* val = toml_table_get_value(root, "Color");
    REQUIRE(val != nullptr);
    CHECK(val->comment == "this is a gray color");
  }

  SUBCASE("Escaped backslashes before quote") {
    const std::string content = "Path = \"C:\\\\\" # windows path\n";
    std::string err;
    auto doc = toml_document_parse(content, &err);
    REQUIRE(doc != nullptr);
    const auto* root = toml_find_table(doc.get(), "");
    REQUIRE(root != nullptr);
    CHECK(toml_table_get_string(root, "Path") == "C:\\");
    const auto* val = toml_table_get_value(root, "Path");
    REQUIRE(val != nullptr);
    CHECK(val->comment == "windows path");
  }

  SUBCASE("Apostrophe in unquoted string value") {
    const std::string content = "Title = Bob's Apple //e # authentic machine\n";
    std::string err;
    auto doc = toml_document_parse(content, &err);
    REQUIRE(doc != nullptr);
    const auto* root = toml_find_table(doc.get(), "");
    REQUIRE(root != nullptr);
    CHECK(toml_table_get_string(root, "Title") == "Bob's Apple //e");
    const auto* val = toml_table_get_value(root, "Title");
    REQUIRE(val != nullptr);
    CHECK(val->comment == "authentic machine");
  }

  SUBCASE("Apple II hex string conversion in toml_table_get_int") {
    auto doc = toml_document_create();
    auto* table = toml_get_or_create_table(doc.get(), "Hardware");
    toml_table_set_string(table, "ResetVector", "$FFFC");
    toml_table_set_string(table, "IoBase", "$C000");

    CHECK(toml_table_get_int(table, "ResetVector") == 0xFFFC);
    CHECK(toml_table_get_int(table, "IoBase") == 0xC000);
  }

  SUBCASE("Array float serialization preserves float type") {
    auto doc = toml_document_create();
    auto* table = toml_get_or_create_table(doc.get(), "Floats");
    TomlArray_t float_arr;
    TomlValue_t v1;
    v1.type = TomlType_t::Float;
    v1.float_val = 1.0;
    TomlValue_t v2;
    v2.type = TomlType_t::Float;
    v2.float_val = 2.5;
    float_arr.push_back(v1);
    float_arr.push_back(v2);
    toml_table_set_array(table, "Rates", float_arr);

    const std::string serialized = toml_document_serialize(doc.get());
    CHECK(serialized.find("1.0") != std::string::npos);

    std::string err;
    auto parsed = toml_document_parse(serialized, &err);
    REQUIRE(parsed != nullptr);
    const auto* parsed_table = toml_find_table(parsed.get(), "Floats");
    REQUIRE(parsed_table != nullptr);
    const auto* arr = toml_table_get_array(parsed_table, "Rates");
    REQUIRE(arr != nullptr);
    REQUIRE(arr->size() == 2);
    CHECK((*arr)[0].type == TomlType_t::Float);
    CHECK((*arr)[0].float_val == doctest::Approx(1.0));
    CHECK((*arr)[1].type == TomlType_t::Float);
    CHECK((*arr)[1].float_val == doctest::Approx(2.5));
  }

  SUBCASE("Strict numeric string parsing rejects trailing non-digits") {
    auto doc = toml_document_create();
    auto* table = toml_get_or_create_table(doc.get(), "Strict");
    toml_table_set_string(table, "BadInt", "100MainStreet");
    toml_table_set_string(table, "BadFloat", "3.14meters");
    toml_table_set_string(table, "BadHex", "$C000extra");

    CHECK(toml_table_get_int(table, "BadInt", 999) == 999);
    CHECK(toml_table_get_double(table, "BadFloat", 99.9) ==
          doctest::Approx(99.9));
    CHECK(toml_table_get_int(table, "BadHex", 888) == 888);
  }
}
