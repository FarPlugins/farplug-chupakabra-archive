#include "msg.h"
#include "utils.hpp"
#include "sysutils.hpp"
#include "farutils.hpp"
#include "common_types.hpp"
#include "ui.hpp"
#include "archive.hpp"

class ArchiveOpener: public IArchiveOpenCallback, public IArchiveOpenVolumeCallback, public ICryptoGetTextPassword, public UnknownImpl, public ProgressMonitor {
private:
  Archive& archive;
  FindData volume_file_info;
  Error error;

  UInt64 total_files;
  UInt64 total_bytes;
  UInt64 completed_files;
  UInt64 completed_bytes;
  virtual void do_update_ui() {
    wostringstream st;
    st << Far::get_msg(MSG_PLUGIN_NAME) << L'\n';
    st << volume_file_info.cFileName << L'\n';
    st << completed_files << L" / " << total_files << L'\n';
    st << Far::get_progress_bar_str(60, completed_files, total_files) << L'\n';
    st << L"\x01\n";
    st << format_data_size(completed_bytes, get_size_suffixes()) << L" / " << format_data_size(total_bytes, get_size_suffixes()) << L'\n';
    st << Far::get_progress_bar_str(60, completed_bytes, total_bytes) << L'\n';
    Far::message(st.str(), 0, FMSG_LEFTALIGN);
  }

  class ArchiveOpenStream: public IInStream, public UnknownImpl {
  private:
    ArchiveOpener& opener;
    HANDLE h_file;
    wstring file_path;
    Error& error;
  public:
    ArchiveOpenStream(ArchiveOpener& opener, const wstring& file_path): opener(opener), file_path(file_path), error(opener.error) {
      h_file = CreateFileW(long_path(file_path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
      CHECK_SYS(h_file != INVALID_HANDLE_VALUE);
    }
    ~ArchiveOpenStream() {
      CloseHandle(h_file);
    }

    UNKNOWN_IMPL_BEGIN
    UNKNOWN_IMPL_ITF(ISequentialInStream)
    UNKNOWN_IMPL_ITF(IInStream)
    UNKNOWN_IMPL_END

    STDMETHODIMP Read(void *data, UInt32 size, UInt32 *processedSize) {
      COM_ERROR_HANDLER_BEGIN
      ERROR_MESSAGE_BEGIN
      DWORD bytes_read;
      CHECK_SYS(ReadFile(h_file, data, size, &bytes_read, NULL));
      if (processedSize)
        *processedSize = bytes_read;
      return S_OK;
      ERROR_MESSAGE_END(file_path)
      COM_ERROR_HANDLER_END
    }

    STDMETHODIMP Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition) {
      COM_ERROR_HANDLER_BEGIN
      ERROR_MESSAGE_BEGIN
      DWORD move_method;
      switch (seekOrigin) {
      case STREAM_SEEK_SET:
        move_method = FILE_BEGIN;
        break;
      case STREAM_SEEK_CUR:
        move_method = FILE_CURRENT;
        break;
      case STREAM_SEEK_END:
        move_method = FILE_END;
        break;
      default:
        return E_INVALIDARG;
      }
      LARGE_INTEGER distance;
      distance.QuadPart = offset;
      LARGE_INTEGER new_position;
      CHECK_SYS(SetFilePointerEx(h_file, distance, &new_position, move_method));
      if (newPosition)
        *newPosition = new_position.QuadPart;
      return S_OK;
      ERROR_MESSAGE_END(file_path)
      COM_ERROR_HANDLER_END
    }
  };

public:
  ArchiveOpener(Archive& archive): archive(archive), volume_file_info(archive.archive_file_info), total_files(0), total_bytes(0), completed_files(0), completed_bytes(0) {
  }

  UNKNOWN_IMPL_BEGIN
  UNKNOWN_IMPL_ITF(IArchiveOpenCallback)
  UNKNOWN_IMPL_ITF(IArchiveOpenVolumeCallback)
  UNKNOWN_IMPL_ITF(ICryptoGetTextPassword)
  UNKNOWN_IMPL_END

  STDMETHODIMP SetTotal(const UInt64 *files, const UInt64 *bytes) {
    COM_ERROR_HANDLER_BEGIN
    if (files) total_files = *files;
    if (bytes) total_bytes = *bytes;
    update_ui();
    return S_OK;
    COM_ERROR_HANDLER_END
  }
  STDMETHODIMP SetCompleted(const UInt64 *files, const UInt64 *bytes) {
    COM_ERROR_HANDLER_BEGIN
    if (files) completed_files = *files;
    if (bytes) completed_bytes = *bytes;
    update_ui();
    return S_OK;
    COM_ERROR_HANDLER_END
  }

  STDMETHODIMP GetProperty(PROPID propID, PROPVARIANT *value) {
    COM_ERROR_HANDLER_BEGIN
    PropVariant var;
    switch (propID) {
    case kpidName:
      var = volume_file_info.cFileName; break;
    case kpidIsDir:
      var = volume_file_info.is_dir(); break;
    case kpidSize:
      var = volume_file_info.size(); break;
    case kpidAttrib:
      var = static_cast<UInt32>(volume_file_info.dwFileAttributes); break;
    case kpidCTime:
      var = volume_file_info.ftCreationTime; break;
    case kpidATime:
      var = volume_file_info.ftLastAccessTime; break;
    case kpidMTime:
      var = volume_file_info.ftLastWriteTime; break;
    }
    var.detach(value);
    return S_OK;
    COM_ERROR_HANDLER_END
  }

  STDMETHODIMP GetStream(const wchar_t *name, IInStream **inStream) {
    COM_ERROR_HANDLER_BEGIN
    wstring file_path = add_trailing_slash(archive.archive_dir) + name;
    ERROR_MESSAGE_BEGIN
    try {
      volume_file_info = get_find_data(file_path);
    }
    catch (Error&) {
      return S_FALSE;
    }
    if (volume_file_info.is_dir())
      return S_FALSE;
    ComObject<IInStream> file_stream(new ArchiveOpenStream(*this, file_path));
    file_stream.detach(inStream);
    update_ui();
    return S_OK;
    ERROR_MESSAGE_END(file_path)
    COM_ERROR_HANDLER_END
  }

  STDMETHODIMP CryptoGetTextPassword(BSTR *password) {
    COM_ERROR_HANDLER_BEGIN
    if (archive.password.empty()) {
      ProgressSuspend ps(*this);
      if (!password_dialog(archive.password))
        FAIL(E_ABORT);
    }
    *password = str_to_bstr(archive.password);
    return S_OK;
    COM_ERROR_HANDLER_END
  }

  bool open_sub_stream(IInArchive* in_arc, ComObject<IInStream>& sub_stream) {
    UInt32 main_subfile;
    PropVariant var;
    if (in_arc->GetArchiveProperty(kpidMainSubfile, &var) != S_OK || var.vt != VT_UI4)
      return false;
    main_subfile = var.ulVal;

    UInt32 num_items;
    if (in_arc->GetNumberOfItems(&num_items) != S_OK || main_subfile >= num_items)
      return false;

    ComObject<IInArchiveGetStream> get_stream;
    if (in_arc->QueryInterface(IID_IInArchiveGetStream, reinterpret_cast<void**>(&get_stream)) != S_OK || !get_stream)
      return false;

    ComObject<ISequentialInStream> sub_seq_stream;
    if (get_stream->GetStream(main_subfile, &sub_seq_stream) != S_OK || !sub_seq_stream)
      return false;

    if (sub_seq_stream->QueryInterface(IID_IInStream, reinterpret_cast<void**>(&sub_stream)) != S_OK || !sub_stream)
      return false;

    return true;
  }

  bool open_archive(const ArcFormat& format, IInStream* in_stream, ComObject<IInArchive>& archive) {
    CHECK_COM(format.arc_lib->CreateObject(reinterpret_cast<const GUID*>(format.class_id.data()), &IID_IInArchive, reinterpret_cast<void**>(&archive)));

    CHECK_COM(in_stream->Seek(0, STREAM_SEEK_SET, NULL));

    const UInt64 max_check_start_position = 1 << 20;
    HRESULT res = archive->Open(in_stream, &max_check_start_position, this);
    if (FAILED(res)) {
      if (error.code != NO_ERROR)
        throw error;
    }
    CHECK_COM(res);
    return res == S_OK;
  }

  void detect(IInStream* in_stream, vector<ComObject<IInArchive>>& archives, vector<vector<ArcFormat>>& formats) {
    vector<ArcFormat> parent_format;
    if (!formats.empty()) parent_format = formats.back();
    for_each(archive.arc_formats.begin(), archive.arc_formats.end(), [&] (const ArcFormat& arc_format) {
      ComObject<IInArchive> archive;
      if (open_archive(arc_format, in_stream, archive)) {
        archives.push_back(archive);
        vector<ArcFormat> new_format(parent_format);
        new_format.push_back(arc_format);
        formats.push_back(new_format);

        ComObject<IInStream> sub_stream;
        if (open_sub_stream(archive, sub_stream))
          detect(sub_stream, archives, formats);
      }
    });
  }

  void open(vector<ComObject<IInArchive>>& archives, vector<vector<ArcFormat>>& formats) {
    ComObject<ArchiveOpenStream> stream(new ArchiveOpenStream(*this, archive.get_file_name()));
    detect(stream, archives, formats);
  }

  void reopen() {
    ComObject<ArchiveOpenStream> stream(new ArchiveOpenStream(*this, archive.get_file_name()));
    vector<ArcFormat>::const_iterator format = archive.formats.begin();
    CHECK(open_archive(*format, stream, archive.in_arc));
    format++;
    while (format != archive.formats.end()) {
      ComObject<IInStream> sub_stream;
      CHECK(open_sub_stream(archive.in_arc, sub_stream));
      CHECK(open_archive(*format, stream, archive.in_arc));
      format++;
    }
  }
};


bool Archive::open(const wstring& file_path) {
  archive_file_info = get_find_data(file_path);
  archive_dir = extract_file_path(file_path);

  ComObject<ArchiveOpener> opener(new ArchiveOpener(*this));
  vector<ComObject<IInArchive>> archives;
  vector<vector<ArcFormat>> formats;
  opener->open(archives, formats);

  if (formats.size() == 0) return false;

  int format_idx;
  if (formats.size() == 1) {
    format_idx = 0;
  }
  else {
    vector<wstring> format_names;
    for (unsigned i = 0; i < formats.size(); i++) {
      wstring name;
      for (unsigned j = 0; j < formats[i].size(); j++) {
        if (!name.empty())
          name += L"->";
        name += formats[i][j].name;
      }
      format_names.push_back(name);
    }
    format_idx = Far::menu(Far::get_msg(MSG_PLUGIN_NAME), format_names);
    if (format_idx == -1) return false;
  }

  in_arc = archives[format_idx];
  this->formats = formats[format_idx];
  return true;
}

void Archive::close() {
  in_arc->Close();
  file_list.clear();
  file_list_index.clear();
}

void Archive::reopen() {
  ComObject<ArchiveOpener> opener(new ArchiveOpener(*this));
  opener->reopen();
}