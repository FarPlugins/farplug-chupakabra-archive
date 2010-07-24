#pragma once

struct PluginItemList: public Array<PluginPanelItem> {
#ifdef FARAPI17
  ObjectArray<Array<unsigned char> > names;
#endif
#ifdef FARAPI18
  ObjectArray<UnicodeString> names;
#endif
  ObjectArray<FarStr> col_str;
  ObjectArray<Array<const FarCh*> > col_data;
};

struct CompositeFileName {
  FarStr long_name;
  // short_name is needed only for correct handling of Unicode file names in Far 1.x
#ifdef FARAPI17
  FarStr short_name;
#endif
  CompositeFileName() {
  }
  explicit CompositeFileName(const FAR_FIND_DATA& find_data): long_name(FAR_FILE_NAME(find_data))
#ifdef FARAPI17
  , short_name(FAR_SHORT_FILE_NAME(find_data))
#endif
  {
  }
  bool operator==(const FAR_FIND_DATA& find_data) const {
#ifdef FARAPI17
    if (short_name.size() && *FAR_SHORT_FILE_NAME(find_data)) return short_name == FAR_SHORT_FILE_NAME(find_data);
    else
#endif
    return long_name == FAR_FILE_NAME(find_data);
  }
};

struct PanelState {
  FarStr directory;
  CompositeFileName current_file;
  CompositeFileName top_panel_file;
  ObjectArray<CompositeFileName> selected_files;
};

class FileListProgress: public ProgressMonitor {
protected:
  void do_update_ui();
public:
  unsigned count;
  FileListProgress(): ProgressMonitor(true), count(0) {
  }
};

class FilePanel {
private:
  enum {
    c_cust_col_cnt = 7
  };
  struct PanelItemData {
    UnicodeString file_name;
    UnicodeString alt_file_name;
    DWORD file_attr;
    FILETIME creation_time;
    FILETIME last_access_time;
    FILETIME last_write_time;
    u64 data_size;
    u64 disk_size;
    u64 valid_size;
    unsigned fragment_cnt;
    unsigned stream_cnt;
    unsigned hard_link_cnt;
    unsigned mft_rec_cnt;
    bool error;
    bool ntfs_attr;
    bool resident;
  };
  UnicodeString current_dir;
  NtfsVolume volume;
#ifdef FARAPI17
  AnsiString current_dir_oem;
#endif // FARAPI17
  ObjectArray<PluginItemList> file_lists;
  PanelMode panel_mode;
  FarStr panel_title;
  FarStr col_types;
  FarStr col_widths;
  FarStr status_col_types;
  FarStr status_col_widths;
  Array<const FarCh*> col_titles;
  Array<unsigned> col_sizes;
  Array<unsigned> col_indices;
  PanelState saved_state;
  static Array<FilePanel*> g_file_panels;
  void parse_column_spec(const UnicodeString& src_col_types, const UnicodeString& src_col_widths, FarStr& col_types, FarStr& col_widths, bool title);
  PluginItemList create_panel_items(const std::list<PanelItemData>& pid_list, bool search_mode);
  void scan_dir(const UnicodeString& root_path, const UnicodeString& rel_path, std::list<PanelItemData>& pid_list, FileListProgress& progress);
  void sort_file_list(std::list<PanelItemData>& pid_list);
  struct FileRecord {
    u64 file_ref_num;
    u64 parent_ref_num;
    UnicodeString file_name;
    DWORD file_attr;
    FILETIME creation_time;
    FILETIME last_access_time;
    FILETIME last_write_time;
    u64 data_size;
    u64 disk_size;
    u64 valid_size;
    u32 fragment_cnt;
    u32 mft_rec_cnt;
    u16 stream_cnt;
    u16 hard_link_cnt;
    u8 flags;
    bool ntfs_attr() const { return (flags & 1) != 0; }
    bool resident() const { return (flags & 2) != 0; }
    void set_flags(bool ntfs_attr, bool resident) { flags = (ntfs_attr ? 1 : 0) | (resident ? 2 : 0); }
  };
  struct FileRecordCompare;
  struct JournalInfo {
    DWORDLONG usn_journal_id;
    USN next_usn;
    JournalInfo(): usn_journal_id(0) {
    }
  };
  struct MftIndex: public ObjectArray<FileRecord>, public JournalInfo {
    void invalidate() {
      clear();
      usn_journal_id = 0;
    }
  };
  MftIndex mft_index;
  u64 root_dir_ref_num;
  void add_file_records(std::list<FileRecord>& file_list, const FileInfo& file_info);
  JournalInfo prepare_usn_journal();
  void delete_usn_journal();
  void create_mft_index();
  void update_mft_index_from_usn();
  void mft_scan_dir(u64 parent_file_index, const UnicodeString& rel_path, std::list<PanelItemData>& pid_list, FileListProgress& progress);
  u64 mft_find_root() const;
  u64 mft_find_path(const UnicodeString& path);
  void store_mft_index();
  void load_mft_index();
  UnicodeString get_mft_index_cache_name();
  FilePanel(){}
public:
  bool flat_mode;
  bool mft_mode;
  static FilePanel* open();
  void apply_saved_state();
  void close();
  void on_close();
  UnicodeString get_current_dir() const {
    return current_dir;
  }
  static FilePanel* FilePanel::get_active_panel();
  void new_file_list(PluginPanelItem*& panel_items, int& item_num, bool search_mode);
  void clear_file_list(void* file_list_ptr);
  void change_directory(const UnicodeString& target_dir, bool search_mode);
  void fill_plugin_info(OpenPluginInfo* info);
  void toggle_mft_mode();
  void reload_mft();
  static void reload_mft_all();
  struct Totals {
    u64 data_size;
    u64 disk_size;
    u64 fragment_cnt;
    unsigned file_cnt;
    unsigned dir_cnt;
    unsigned hl_cnt;
    unsigned file_rp_cnt;
    unsigned dir_rp_cnt;
    Totals() {
      memset(this, 0, sizeof(*this));
    }
  };
  Totals mft_get_totals(const ObjectArray<UnicodeString>& file_list);
};

bool show_file_panel_mode_dialog(FilePanelMode& mode);