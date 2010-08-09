#include "msg.h"
#include "utils.hpp"
#include "sysutils.hpp"
#include "farutils.hpp"
#include "common_types.hpp"
#include "ui.hpp"
#include "archive.hpp"

bool retry_or_ignore_error(const wstring& path, const Error& error, bool& ignore_errors, ErrorLog& error_log, ProgressMonitor& progress) {
  bool ignore = ignore_errors;
  if (!ignore) {
    ProgressSuspend ps(progress);
    switch (error_retry_ignore_dialog(path, error, true)) {
    case rdrRetry:
      break;
    case rdrIgnore:
      ignore = true;
      break;
    case rdrIgnoreAll:
      ignore = true;
      ignore_errors = true;
      break;
    case rdrCancel:
      FAIL(E_ABORT);
    }
  }
  if (ignore) {
    error_log.add(path, error);
    return true;
  }
  return false;
}

void ignore_error(const wstring& path, const Error& error, bool& ignore_errors, ErrorLog& error_log, ProgressMonitor& progress) {
  if (!ignore_errors) {
    ProgressSuspend ps(progress);
    switch (error_retry_ignore_dialog(path, error, false)) {
    case rdrIgnore:
      break;
    case rdrIgnoreAll:
      ignore_errors = true;
      break;
    case rdrCancel:
      FAIL(E_ABORT);
    }
  }
  error_log.add(path, error);
}

FindData convert_file_info(const FileInfo& file_info) {
  FindData find_data;
  memset(&find_data, 0, sizeof(find_data));
  find_data.dwFileAttributes = file_info.attr;
  find_data.ftCreationTime = file_info.ctime;
  find_data.ftLastAccessTime = file_info.atime;
  find_data.ftLastWriteTime = file_info.mtime;
  find_data.nFileSizeHigh = file_info.size >> 32;
  find_data.nFileSizeLow = file_info.size & 0xFFFFFFFF;
  wcscpy(find_data.cFileName, file_info.name.c_str());
  return find_data;
}


class ExtractProgress: public ProgressMonitor {
private:
  unsigned __int64 completed;
  unsigned __int64 total;
  wstring file_path;
  unsigned __int64 file_completed;
  unsigned __int64 file_total;

  virtual void do_update_ui() {
    const unsigned c_width = 60;
    wostringstream st;
    st << Far::get_msg(MSG_PLUGIN_NAME) << L'\n';

    unsigned file_percent;
    if (file_total == 0)
      file_percent = 0;
    else
      file_percent = round(static_cast<double>(file_completed) / file_total * 100);
    if (file_percent > 100)
      file_percent = 100;

    unsigned percent;
    if (total == 0)
      percent = 0;
    else
      percent = round(static_cast<double>(completed) / total * 100);
    if (percent > 100)
      percent = 100;

    unsigned __int64 speed;
    if (time_elapsed() == 0)
      speed = 0;
    else
      speed = round(static_cast<double>(completed) / time_elapsed() * ticks_per_sec());

    st << Far::get_msg(MSG_PROGRESS_EXTRACT) << L'\n';
    st << fit_str(file_path, c_width) << L'\n';
    st << setw(7) << format_data_size(file_completed, get_size_suffixes()) << L" / " << format_data_size(file_total, get_size_suffixes()) << L'\n';
    st << Far::get_progress_bar_str(c_width, file_percent, 100) << L'\n';
    st << L"\x1\n";

    st << setw(7) << format_data_size(completed, get_size_suffixes()) << L" / " << format_data_size(total, get_size_suffixes()) << L" @ " << setw(9) << format_data_size(speed, get_speed_suffixes()) << L'\n';
    st << Far::get_progress_bar_str(c_width, percent, 100) << L'\n';

    Far::message(st.str(), 0, FMSG_LEFTALIGN);

    Far::set_progress_state(TBPF_NORMAL);
    Far::set_progress_value(percent, 100);

    SetConsoleTitleW((L"{" + int_to_str(percent) + L"%} " + Far::get_msg(MSG_PROGRESS_EXTRACT)).c_str());
  }

public:
  ExtractProgress(): ProgressMonitor(true), completed(0), total(0), file_completed(0), file_total(0) {
  }

  void on_create_file(const wstring& file_path, unsigned __int64 size) {
    this->file_path = file_path;
    file_total = size;
    file_completed = 0;
    update_ui();
  }
  void on_write_file(unsigned size_written) {
    file_completed += size_written;
    update_ui();
  }
  void on_total_update(UInt64 total) {
    this->total = total;
    update_ui();
  }
  void on_completed_update(UInt64 completed) {
    this->completed = completed;
    update_ui();
  }
};


class FileExtractStream: public ISequentialOutStream, public UnknownImpl {
private:
  HANDLE h_file;
  const wstring& file_path;
  const FileInfo& file_info;
  ExtractProgress& progress;
  bool& ignore_errors;
  ErrorLog& error_log;
  Error& error;
  bool error_state;

public:
  FileExtractStream(const wstring& file_path, const FileInfo& file_info, ExtractProgress& progress, bool& ignore_errors, ErrorLog& error_log, Error& error): h_file(INVALID_HANDLE_VALUE), file_path(file_path), file_info(file_info), progress(progress), ignore_errors(ignore_errors), error_log(error_log), error(error), error_state(false) {
    progress.on_create_file(file_path, file_info.size);
    while (true) {
      try {
        h_file = CreateFileW(long_path(file_path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        CHECK_SYS(h_file != INVALID_HANDLE_VALUE);
        break;
      }
      catch (const Error& e) {
        error_state = true;
        if (retry_or_ignore_error(file_path, e, ignore_errors, error_log, progress))
          break;
      }
    }
  }
  ~FileExtractStream() {
    SetEndOfFile(h_file);
    if (h_file != INVALID_HANDLE_VALUE)
      CloseHandle(h_file);
    if (error_state)
      DeleteFileW(long_path(file_path).c_str());
  }

  UNKNOWN_IMPL_BEGIN
  UNKNOWN_IMPL_ITF(ISequentialOutStream)
  UNKNOWN_IMPL_END

  STDMETHODIMP Write(const void *data, UInt32 size, UInt32 *processedSize) {
    COM_ERROR_HANDLER_BEGIN
    if (error_state) {
      *processedSize = size;
    }
    else {
      try {
        CHECK_SYS(WriteFile(h_file, data, size, reinterpret_cast<LPDWORD>(processedSize), NULL));
      }
      catch (const Error& e) {
        error_state = true;
        *processedSize = size;
        ignore_error(file_path, e, ignore_errors, error_log, progress);
      }
      progress.on_write_file(*processedSize);
    }
    return S_OK;
    COM_ERROR_HANDLER_END;
  }

  void allocate() {
    if (file_info.size) {
      LARGE_INTEGER position;
      position.QuadPart = file_info.size;
      CHECK_SYS(SetFilePointerEx(h_file, position, NULL, FILE_BEGIN));
      CHECK_SYS(SetEndOfFile(h_file));
      position.QuadPart = 0;
      CHECK_SYS(SetFilePointerEx(h_file, position, NULL, FILE_BEGIN));
    }
  }
};


class ArchiveExtractor: public IArchiveExtractCallback, public ICryptoGetTextPassword, public UnknownImpl, public ExtractProgress {
private:
  wstring file_path;
  FileInfo file_info;
  UInt32 src_dir_index;
  wstring dst_dir;
  const FileList& file_list;
  wstring& password;
  OverwriteOption& oo;
  bool& ignore_errors;
  Error& error;
  ErrorLog& error_log;

public:
  ArchiveExtractor(UInt32 src_dir_index, const wstring& dst_dir, const FileList& file_list, wstring& password, OverwriteOption& oo, bool& ignore_errors, ErrorLog& error_log, Error& error): src_dir_index(src_dir_index), dst_dir(dst_dir), file_list(file_list), password(password), oo(oo), ignore_errors(ignore_errors), error_log(error_log), error(error) {
  }

  UNKNOWN_IMPL_BEGIN
  UNKNOWN_IMPL_ITF(IProgress)
  UNKNOWN_IMPL_ITF(IArchiveExtractCallback)
  UNKNOWN_IMPL_ITF(ICryptoGetTextPassword)
  UNKNOWN_IMPL_END

  STDMETHODIMP SetTotal(UInt64 total) {
    COM_ERROR_HANDLER_BEGIN
    on_total_update(total);
    return S_OK;
    COM_ERROR_HANDLER_END
  }
  STDMETHODIMP SetCompleted(const UInt64 *completeValue) {
    COM_ERROR_HANDLER_BEGIN
    if (completeValue)
      on_completed_update(*completeValue);
    return S_OK;
    COM_ERROR_HANDLER_END
  }

  STDMETHODIMP GetStream(UInt32 index, ISequentialOutStream **outStream,  Int32 askExtractMode) {
    COM_ERROR_HANDLER_BEGIN
    if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) {
      *outStream = nullptr;
      return S_OK;
    }

    file_info = file_list[index];
    file_path = file_info.name;
    UInt32 parent_index = file_info.parent;
    while (parent_index != src_dir_index) {
      const FileInfo& file_info = file_list[parent_index];
      file_path.insert(0, 1, L'\\').insert(0, file_info.name);
      parent_index = file_info.parent;
    }
    file_path.insert(0, 1, L'\\').insert(0, dst_dir);

    FindData dst_file_info;
    HANDLE h_find = FindFirstFileW(long_path(file_path).c_str(), &dst_file_info);
    if (h_find != INVALID_HANDLE_VALUE) {
      FindClose(h_find);
      bool overwrite;
      if (oo == ooAsk) {
        FindData src_file_info = convert_file_info(file_info);
        ProgressSuspend ps(*this);
        OverwriteAction oa = overwrite_dialog(file_path, src_file_info, dst_file_info);
        if (oa == oaYes)
          overwrite = true;
        else if (oa == oaYesAll) {
          overwrite = true;
          oo = ooOverwrite;
        }
        else if (oa == oaNo)
          overwrite = false;
        else if (oa == oaNoAll) {
          overwrite = false;
          oo = ooSkip;
        }
        else if (oa == oaCancel)
          return E_ABORT;
      }
      else if (oo == ooOverwrite)
        overwrite = true;
      else if (oo == ooSkip)
        overwrite = false;

      if (overwrite) {
        SetFileAttributesW(long_path(file_path).c_str(), FILE_ATTRIBUTE_NORMAL);
      }
      else {
        *outStream = nullptr;
        return S_OK;
      }
    }

    FileExtractStream* file_extract_stream = new FileExtractStream(file_path, file_info, *this, ignore_errors, error_log, error);
    ComObject<ISequentialOutStream> out_stream(file_extract_stream);
    file_extract_stream->allocate();
    out_stream.detach(outStream);

    return S_OK;
    COM_ERROR_HANDLER_END
  }
  STDMETHODIMP PrepareOperation(Int32 askExtractMode) {
    COM_ERROR_HANDLER_BEGIN
    return S_OK;
    COM_ERROR_HANDLER_END
  }
  STDMETHODIMP SetOperationResult(Int32 resultEOperationResult) {
    COM_ERROR_HANDLER_BEGIN
    try {
      if (resultEOperationResult == NArchive::NExtract::NOperationResult::kUnSupportedMethod)
        FAIL_MSG(Far::get_msg(MSG_ERROR_EXTRACT_UNSUPPORTED_METHOD));
      else if (resultEOperationResult == NArchive::NExtract::NOperationResult::kDataError)
        FAIL_MSG(Far::get_msg(MSG_ERROR_EXTRACT_DATA_ERROR));
      else if (resultEOperationResult == NArchive::NExtract::NOperationResult::kCRCError)
        FAIL_MSG(Far::get_msg(MSG_ERROR_EXTRACT_CRC_ERROR));
      else
        return S_OK;
    }
    catch (const Error& e) {
      ignore_error(file_path, e, ignore_errors, error_log, *this);
    }
    COM_ERROR_HANDLER_END
  }

  STDMETHODIMP CryptoGetTextPassword(BSTR *pwd) {
    COM_ERROR_HANDLER_BEGIN
    if (password.empty()) {
      ProgressSuspend ps(*this);
      if (!password_dialog(password))
        FAIL(E_ABORT);
    }
    *pwd = str_to_bstr(password);
    return S_OK;
    COM_ERROR_HANDLER_END
  }
};

void Archive::prepare_dst_dir(const wstring& path) {
  if (!is_root_path(path)) {
    prepare_dst_dir(extract_file_path(path));
    CreateDirectoryW(long_path(path).c_str(), NULL);
  }
}

class PrepareExtractProgress: public ProgressMonitor {
private:
  const wstring* file_path;

  virtual void do_update_ui() {
    const unsigned c_width = 60;
    wostringstream st;
    st << Far::get_msg(MSG_PLUGIN_NAME) << L'\n';

    st << Far::get_msg(MSG_PROGRESS_CREATE_DIRS) << L'\n';
    st << left << setw(c_width) << fit_str(*file_path, c_width) << L'\n';

    Far::message(st.str(), 0, FMSG_LEFTALIGN);

    Far::set_progress_state(TBPF_INDETERMINATE);

    SetConsoleTitleW(Far::get_msg(MSG_PROGRESS_CREATE_DIRS).c_str());
  }

public:
  PrepareExtractProgress(): ProgressMonitor(true) {
  }
  void update(const wstring& file_path) {
    this->file_path = &file_path;
    update_ui();
  }
};

void Archive::prepare_extract(UInt32 file_index, const wstring& parent_dir, list<UInt32>& indices, const FileList& file_list, bool& ignore_errors, ErrorLog& error_log, PrepareExtractProgress& progress) {
  const FileInfo& file_info = file_list[file_index];
  if (file_info.is_dir()) {
    wstring dir_path = add_trailing_slash(parent_dir) + file_info.name;
    progress.update(dir_path);

    while (true) {
      try {
        BOOL res = CreateDirectoryW(long_path(dir_path).c_str(), NULL);
        if (!res) {
          CHECK_SYS(GetLastError() == ERROR_ALREADY_EXISTS);
        }
        break;
      }
      catch (const Error& e) {
        if (retry_or_ignore_error(dir_path, e, ignore_errors, error_log, progress))
          break;
      }
    }

    FileIndexRange dir_list = get_dir_list(file_index);
    for_each(dir_list.first, dir_list.second, [&] (UInt32 file_index) {
      prepare_extract(file_index, dir_path, indices, file_list, ignore_errors, error_log, progress);
    });
  }
  else {
    indices.push_back(file_index);
  }
}

class SetAttrProgress: public ProgressMonitor {
private:
  const wstring* file_path;

  virtual void do_update_ui() {
    const unsigned c_width = 60;
    wostringstream st;
    st << Far::get_msg(MSG_PLUGIN_NAME) << L'\n';

    st << Far::get_msg(MSG_PROGRESS_SET_ATTR) << L'\n';
    st << left << setw(c_width) << fit_str(*file_path, c_width) << L'\n';
    Far::message(st.str(), 0, FMSG_LEFTALIGN);

    Far::set_progress_state(TBPF_INDETERMINATE);

    SetConsoleTitleW(Far::get_msg(MSG_PROGRESS_SET_ATTR).c_str());
  }

public:
  SetAttrProgress(): ProgressMonitor(true) {
  }
  void update(const wstring& file_path) {
    this->file_path = &file_path;
    update_ui();
  }
};

void Archive::set_attr(UInt32 file_index, const wstring& parent_dir, bool& ignore_errors, ErrorLog& error_log, SetAttrProgress& progress) {
  const FileInfo& file_info = file_list[file_index];
  wstring file_path = add_trailing_slash(parent_dir) + file_info.name;
  progress.update(file_path);
  if (file_info.is_dir()) {
    FileIndexRange dir_list = get_dir_list(file_index);
    for_each (dir_list.first, dir_list.second, [&] (UInt32 file_index) {
      if (file_list[file_index].is_dir()) {
        set_attr(file_index, file_path, ignore_errors, error_log, progress);
      }
    });
  }
  while (true) {
    try {
      CHECK_SYS(SetFileAttributesW(long_path(file_path).c_str(), FILE_ATTRIBUTE_NORMAL));
      File file(file_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
      CHECK_SYS(SetFileAttributesW(long_path(file_path).c_str(), file_info.attr));
      file.set_time(&file_info.ctime, &file_info.atime, &file_info.mtime);
      break;
    }
    catch (const Error& e) {
      if (retry_or_ignore_error(file_path, e, ignore_errors, error_log, progress))
        break;
    }
  }
}

void Archive::extract(UInt32 src_dir_index, const vector<UInt32>& src_indices, const ExtractOptions& options, ErrorLog& error_log) {
  bool ignore_errors = options.ignore_errors;
  OverwriteOption overwrite_option = options.overwrite;

  prepare_dst_dir(options.dst_dir);

  PrepareExtractProgress prepare_extract_progress;
  list<UInt32> file_indices;
  for (unsigned i = 0; i < src_indices.size(); i++) {
    prepare_extract(src_indices[i], options.dst_dir, file_indices, file_list, ignore_errors, error_log, prepare_extract_progress);
  }

  vector<UInt32> indices;
  indices.reserve(file_indices.size());
  copy(file_indices.begin(), file_indices.end(), back_insert_iterator<vector<UInt32>>(indices));
  sort(indices.begin(), indices.end());

  Error error;
  ComObject<IArchiveExtractCallback> extractor(new ArchiveExtractor(src_dir_index, options.dst_dir, file_list, password, overwrite_option, ignore_errors, error_log, error));
  HRESULT res = in_arc->Extract(indices.data(), indices.size(), 0, extractor);
  if (FAILED(res)) {
    if (error)
      throw error;
    else
      FAIL(res);
  }

  SetAttrProgress set_attr_progress;
  for (unsigned i = 0; i < src_indices.size(); i++) {
    const FileInfo& file_info = file_list[src_indices[i]];
    set_attr(src_indices[i], options.dst_dir, ignore_errors, error_log, set_attr_progress);
  }
}
