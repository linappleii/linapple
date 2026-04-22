#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/FileBrowser.h"

// Helper to construct a file_entry_t for testing
static file_entry_t create_entry(const char* name, FileEntryType type,
                                 uint64_t size) {
  file_entry_t entry{};
  entry.name[0] = '\0';
  if (name) Util_SafeStrCpy(entry.name, name, sizeof(entry.name));
  entry.type = type;
  entry.size = size;
  return entry;
}

TEST_CASE("FileBrowser: file_entry_t Properties") {
  file_entry_t entry = create_entry("test.dsk", FILE_ENTRY_FILE, 1024);
  CHECK(strcmp(entry.name, "test.dsk") == 0);
  CHECK(entry.type == FILE_ENTRY_FILE);
  CHECK(entry.size == 1024);
  CHECK(FileEntry_IsDirType(&entry) == false);
}

TEST_CASE("FileBrowser: file_entry_t Size Formatting") {
  char buf[32];

  file_entry_t f1 = create_entry("small", FILE_ENTRY_FILE, 500);
  FileEntry_FormatTypeOrSize(&f1, buf, sizeof(buf));
  CHECK(std::string(buf) == "500");

  file_entry_t f2 = create_entry("kb", FILE_ENTRY_FILE, 1024 * 5);
  FileEntry_FormatTypeOrSize(&f2, buf, sizeof(buf));
  CHECK(std::string(buf) == "5K");

  file_entry_t f3 = create_entry("mb", FILE_ENTRY_FILE, 1024 * 1024 * 2);
  FileEntry_FormatTypeOrSize(&f3, buf, sizeof(buf));
  CHECK(std::string(buf) == "2M");

  file_entry_t f4 =
      create_entry("gb", FILE_ENTRY_FILE, 1024ULL * 1024ULL * 1024ULL * 3ULL);
  FileEntry_FormatTypeOrSize(&f4, buf, sizeof(buf));
  CHECK(std::string(buf) == "3G");

  file_entry_t d = create_entry("dir", FILE_ENTRY_DIR, 0);
  FileEntry_FormatTypeOrSize(&d, buf, sizeof(buf));
  CHECK(std::string(buf) == "<DIR>");

  file_entry_t u = create_entry("up", FILE_ENTRY_UP, 0);
  FileEntry_FormatTypeOrSize(&u, buf, sizeof(buf));
  CHECK(std::string(buf) == "<UP>");
}

// Helper to create a dummy file
void create_dummy_file(const std::string& path, size_t size) {
  std::ofstream f(path, std::ios::binary);
  if (size > 0) {
    std::vector<char> dummy(size, 0);
    f.write(dummy.data(), size);
  }
}

TEST_CASE("FileBrowser: LocalFileListGenerator") {
  // Setup temp directory
  std::string test_dir = "test_browser_temp";
  mkdir(test_dir.c_str(), 0755);
  mkdir((test_dir + "/subdir").c_str(), 0755);
  create_dummy_file(test_dir + "/file1.dsk", 1024);
  create_dummy_file(test_dir + "/file2.po", 2048);

  SUBCASE("List Generation") {
    FileListGenerator_t* gen =
        FileBrowser_CreateLocalGenerator(test_dir.c_str());
    REQUIRE(gen != nullptr);

    FileList_t* list = gen->generate_file_list(gen);
    REQUIRE(list != nullptr);

    // Should have: .. (UP), subdir (DIR), file1 (FILE), file2 (FILE)
    REQUIRE(FileBrowser_GetCount(list) == 4);

    const file_entry_t* e0 = FileBrowser_GetEntry(list, 0);
    const file_entry_t* e1 = FileBrowser_GetEntry(list, 1);
    const file_entry_t* e2 = FileBrowser_GetEntry(list, 2);
    const file_entry_t* e3 = FileBrowser_GetEntry(list, 3);

    REQUIRE(e0 != nullptr);
    CHECK(e0->type == FILE_ENTRY_UP);

    REQUIRE(e1 != nullptr);
    CHECK(e1->type == FILE_ENTRY_DIR);
    CHECK(strcmp(e1->name, "subdir") == 0);

    REQUIRE(e2 != nullptr);
    CHECK(e2->type == FILE_ENTRY_FILE);
    CHECK(strcmp(e2->name, "file1.dsk") == 0);

    REQUIRE(e3 != nullptr);
    CHECK(e3->type == FILE_ENTRY_FILE);
    CHECK(strcmp(e3->name, "file2.po") == 0);

    FileBrowser_FreeList(list);
    gen->destroy(gen);
  }

  SUBCASE("Failure Handling") {
    FileListGenerator_t* gen =
        FileBrowser_CreateLocalGenerator("non_existent_directory_xyz");
    REQUIRE(gen != nullptr);

    FileList_t* list = gen->generate_file_list(gen);
    REQUIRE(list != nullptr);

    CHECK(FileBrowser_GetCount(list) == 0);
    CHECK(strcmp(FileBrowser_GetFailureMessage(list), "(success)") != 0);

    FileBrowser_FreeList(list);
    gen->destroy(gen);
  }

  // Cleanup
  unlink((test_dir + "/file1.dsk").c_str());
  unlink((test_dir + "/file2.po").c_str());
  rmdir((test_dir + "/subdir").c_str());
  rmdir(test_dir.c_str());
}

TEST_CASE("FileBrowser: C API Null Checks") {
  CHECK(FileBrowser_CreateLocalGenerator(nullptr) == nullptr);
  CHECK(FileBrowser_GetCount(nullptr) == 0);
  CHECK(FileBrowser_GetEntry(nullptr, 0) == nullptr);
  CHECK(strcmp(FileBrowser_GetFailureMessage(nullptr), "Null list handle") ==
        0);

  char buf[32];
  FileEntry_FormatTypeOrSize(nullptr, buf, sizeof(buf));  // Should not crash

  CHECK(FileEntry_IsDirType(nullptr) == false);
}
