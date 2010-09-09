// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/platform_file.h"
#include "base/scoped_temp_dir.h"
#include "base/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Reads from a file the given number of bytes, or until EOF is reached.
// Returns the number of bytes read.
int ReadFully(base::PlatformFile file, int64 offset, char* data, int size) {
  int total_bytes_read = 0;
  int bytes_read;
  while (total_bytes_read < size) {
    bytes_read = base::ReadPlatformFile(
        file, offset + total_bytes_read, &data[total_bytes_read],
        size - total_bytes_read);

    // If we reached EOF, bytes_read will be 0.
    if (bytes_read == 0)
      return total_bytes_read;

    if ((bytes_read < 0) || (bytes_read > size - total_bytes_read))
      return -1;

    total_bytes_read += bytes_read;
  }

  return total_bytes_read;
}

// Writes the given number of bytes to a file.
// Returns the number of bytes written.
int WriteFully(base::PlatformFile file, int64 offset,
               const char* data, int size) {
  int total_bytes_written = 0;
  int bytes_written;
  while (total_bytes_written < size) {
    bytes_written = base::WritePlatformFile(
        file, offset + total_bytes_written, &data[total_bytes_written],
        size - total_bytes_written);

    if ((bytes_written == 0) && (size == 0))
      return 0;

    if ((bytes_written <= 0) || (bytes_written > size - total_bytes_written))
      return -1;

    total_bytes_written += bytes_written;
  }

  return total_bytes_written;
}

} // namespace

TEST(PlatformFile, CreatePlatformFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.path().AppendASCII("create_file_1");

  // Open a file that doesn't exist.
  base::PlatformFileError error_code = base::PLATFORM_FILE_OK;
  base::PlatformFile file = base::CreatePlatformFile(
      file_path, base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ,
      NULL, &error_code);
  EXPECT_EQ(base::kInvalidPlatformFileValue, file);
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, error_code);

  // Open or create a file.
  bool created = false;
  error_code = base::PLATFORM_FILE_OK;
  file = base::CreatePlatformFile(
      file_path, base::PLATFORM_FILE_OPEN_ALWAYS | base::PLATFORM_FILE_READ,
      &created, &error_code);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);
  EXPECT_TRUE(created);
  EXPECT_EQ(base::PLATFORM_FILE_OK, error_code);
  base::ClosePlatformFile(file);

  // Open an existing file.
  created = false;
  file = base::CreatePlatformFile(
      file_path, base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ,
      &created, &error_code);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);
  EXPECT_FALSE(created);
  EXPECT_EQ(base::PLATFORM_FILE_OK, error_code);
  base::ClosePlatformFile(file);

  // Create a file that exists.
  file = base::CreatePlatformFile(
      file_path, base::PLATFORM_FILE_CREATE | base::PLATFORM_FILE_READ,
      &created, &error_code);
  EXPECT_EQ(base::kInvalidPlatformFileValue, file);
  EXPECT_FALSE(created);
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, error_code);

  // Create or overwrite a file.
  error_code = base::PLATFORM_FILE_OK;
  file = base::CreatePlatformFile(
      file_path, base::PLATFORM_FILE_CREATE_ALWAYS | base::PLATFORM_FILE_READ,
      &created, &error_code);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);
  EXPECT_TRUE(created);
  EXPECT_EQ(base::PLATFORM_FILE_OK, error_code);
  base::ClosePlatformFile(file);

  // Create a delete-on-close file.
  created = false;
  file_path = temp_dir.path().AppendASCII("create_file_2");
  file = base::CreatePlatformFile(
      file_path,
      base::PLATFORM_FILE_OPEN_ALWAYS |
      base::PLATFORM_FILE_DELETE_ON_CLOSE |
      base::PLATFORM_FILE_READ,
      &created, &error_code);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);
  EXPECT_TRUE(created);
  EXPECT_EQ(base::PLATFORM_FILE_OK, error_code);

  EXPECT_TRUE(base::ClosePlatformFile(file));
  EXPECT_FALSE(file_util::PathExists(file_path));
}

TEST(PlatformFile, ReadWritePlatformFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.path().AppendASCII("read_write_file");
  base::PlatformFile file = base::CreatePlatformFile(
      file_path,
      base::PLATFORM_FILE_CREATE |
      base::PLATFORM_FILE_READ |
      base::PLATFORM_FILE_WRITE,
      NULL, NULL);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);

  char data_to_write[] = "test";
  const int kTestDataSize = 4;

  // Write 0 bytes to the file.
  int bytes_written = WriteFully(file, 0, data_to_write, 0);
  EXPECT_EQ(0, bytes_written);

  // Write "test" to the file.
  bytes_written = WriteFully(file, 0, data_to_write, kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  // Read from EOF.
  char data_read_1[32];
  int bytes_read = ReadFully(file, kTestDataSize, data_read_1, kTestDataSize);
  EXPECT_EQ(0, bytes_read);

  // Read from somewhere in the middle of the file.
  const int kPartialReadOffset = 1;
  bytes_read = ReadFully(file, kPartialReadOffset, data_read_1, kTestDataSize);
  EXPECT_EQ(kTestDataSize - kPartialReadOffset, bytes_read);
  for (int i = 0; i < bytes_read; i++)
    EXPECT_EQ(data_to_write[i + kPartialReadOffset], data_read_1[i]);

  // Read 0 bytes.
  bytes_read = ReadFully(file, 0, data_read_1, 0);
  EXPECT_EQ(0, bytes_read);

  // Read the entire file.
  bytes_read = ReadFully(file, 0, data_read_1, kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_read);
  for (int i = 0; i < bytes_read; i++)
    EXPECT_EQ(data_to_write[i], data_read_1[i]);

  // Write past the end of the file.
  const int kOffsetBeyondEndOfFile = 10;
  const int kPartialWriteLength = 2;
  bytes_written = WriteFully(file, kOffsetBeyondEndOfFile,
                             data_to_write, kPartialWriteLength);
  EXPECT_EQ(kPartialWriteLength, bytes_written);

  // Make sure the file was extended.
  int64 file_size = 0;
  EXPECT_TRUE(file_util::GetFileSize(file_path, &file_size));
  EXPECT_EQ(kOffsetBeyondEndOfFile + kPartialWriteLength, file_size);

  // Make sure the file was zero-padded.
  char data_read_2[32];
  bytes_read = ReadFully(file, 0, data_read_2, static_cast<int>(file_size));
  EXPECT_EQ(file_size, bytes_read);
  for (int i = 0; i < kTestDataSize; i++)
    EXPECT_EQ(data_to_write[i], data_read_2[i]);
  for (int i = kTestDataSize; i < kOffsetBeyondEndOfFile; i++)
    EXPECT_EQ(0, data_read_2[i]);
  for (int i = kOffsetBeyondEndOfFile; i < file_size; i++)
    EXPECT_EQ(data_to_write[i - kOffsetBeyondEndOfFile], data_read_2[i]);

  // Close the file handle to allow the temp directory to be deleted.
  base::ClosePlatformFile(file);
}

TEST(PlatformFile, TruncatePlatformFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.path().AppendASCII("truncate_file");
  base::PlatformFile file = base::CreatePlatformFile(
      file_path,
      base::PLATFORM_FILE_CREATE |
      base::PLATFORM_FILE_READ |
      base::PLATFORM_FILE_WRITE,
      NULL, NULL);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);

  // Write "test" to the file.
  char data_to_write[] = "test";
  int kTestDataSize = 4;
  int bytes_written = WriteFully(file, 0, data_to_write, kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  // Extend the file.
  const int kExtendedFileLength = 10;
  int64 file_size = 0;
  EXPECT_TRUE(base::TruncatePlatformFile(file, kExtendedFileLength));
  EXPECT_TRUE(file_util::GetFileSize(file_path, &file_size));
  EXPECT_EQ(kExtendedFileLength, file_size);

  // Make sure the file was zero-padded.
  char data_read[32];
  int bytes_read = ReadFully(file, 0, data_read, static_cast<int>(file_size));
  EXPECT_EQ(file_size, bytes_read);
  for (int i = 0; i < kTestDataSize; i++)
    EXPECT_EQ(data_to_write[i], data_read[i]);
  for (int i = kTestDataSize; i < file_size; i++)
    EXPECT_EQ(0, data_read[i]);

  // Truncate the file.
  const int kTruncatedFileLength = 2;
  EXPECT_TRUE(base::TruncatePlatformFile(file, kTruncatedFileLength));
  EXPECT_TRUE(file_util::GetFileSize(file_path, &file_size));
  EXPECT_EQ(kTruncatedFileLength, file_size);

  // Make sure the file was truncated.
  bytes_read = ReadFully(file, 0, data_read, kTestDataSize);
  EXPECT_EQ(file_size, bytes_read);
  for (int i = 0; i < file_size; i++)
    EXPECT_EQ(data_to_write[i], data_read[i]);

  // Close the file handle to allow the temp directory to be deleted.
  base::ClosePlatformFile(file);
}

TEST(PlatformFile, TouchGetInfoPlatformFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::PlatformFile file = base::CreatePlatformFile(
      temp_dir.path().AppendASCII("touch_get_info_file"),
      base::PLATFORM_FILE_CREATE |
      base::PLATFORM_FILE_WRITE |
      base::PLATFORM_FILE_WRITE_ATTRIBUTES,
      NULL, NULL);
  EXPECT_NE(base::kInvalidPlatformFileValue, file);

  // Get info for a newly created file.
  base::PlatformFileInfo info;
  EXPECT_TRUE(base::GetPlatformFileInfo(file, &info));
  base::Time now = base::Time::Now();
  EXPECT_EQ(0, info.size);
  EXPECT_FALSE(info.is_directory);
  EXPECT_TRUE(info.last_accessed <= now);
  EXPECT_TRUE(info.last_modified <= now);
  EXPECT_TRUE(info.creation_time <= now);
  base::Time creation_time = info.creation_time;

  // Write "test" to the file.
  char data[] = "test";
  const int kTestDataSize = 4;
  int bytes_written = WriteFully(file, 0, data, kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  // Change the last_accessed and last_modified dates.
  base::Time new_last_accessed = now + base::TimeDelta::FromSeconds(123);
  base::Time new_last_modified = now + base::TimeDelta::FromMinutes(456);

  EXPECT_TRUE(base::TouchPlatformFile(file, new_last_accessed,
                                      new_last_modified));

  // Make sure the file info was updated accordingly.
  EXPECT_TRUE(base::GetPlatformFileInfo(file, &info));
  EXPECT_EQ(info.size, kTestDataSize);
  EXPECT_FALSE(info.is_directory);

  // ext2/ext3 and HPS/HPS+ seem to have a timestamp granularity of 1s.
#if defined(OS_POSIX)
  EXPECT_EQ(info.last_accessed.ToTimeVal().tv_sec,
            new_last_accessed.ToTimeVal().tv_sec);
  EXPECT_EQ(info.last_modified.ToTimeVal().tv_sec,
            new_last_modified.ToTimeVal().tv_sec);
#else
  EXPECT_TRUE(info.last_accessed == new_last_accessed);
  EXPECT_TRUE(info.last_modified == new_last_modified);
#endif

  EXPECT_TRUE(info.creation_time == creation_time);

  // Close the file handle to allow the temp directory to be deleted.
  base::ClosePlatformFile(file);
}
