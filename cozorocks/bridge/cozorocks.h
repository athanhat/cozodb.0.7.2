//
// Created by Ziyang Hu on 2022/4/13.
//

#pragma once

#include <memory>
#include<iostream>
#include <shared_mutex>
#include "rust/cxx.h"

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"
#include "rocksdb/utilities/optimistic_transaction_db.h"


typedef std::shared_mutex Lock;
typedef std::unique_lock<Lock> WriteLock;
typedef std::shared_lock<Lock> ReadLock;


using namespace ROCKSDB_NAMESPACE;
using std::unique_ptr;
using std::shared_ptr;
using std::make_unique;
using std::make_shared;
using std::string;
using std::vector;
using std::unordered_map;
using std::tuple;

struct BridgeStatus;

typedef Status::Code StatusCode;
typedef Status::SubCode StatusSubCode;
typedef Status::Severity StatusSeverity;

inline Slice convert_slice(rust::Slice<const uint8_t> d) {
    return Slice(reinterpret_cast<const char *>(d.data()), d.size());
}

inline rust::Slice<const uint8_t> convert_slice_back(const Slice &s) {
    return rust::Slice(reinterpret_cast<const std::uint8_t *>(s.data()), s.size());
}


inline rust::Slice<const uint8_t> convert_pinnable_slice_back(const PinnableSlice &s) {
    return rust::Slice(reinterpret_cast<const std::uint8_t *>(s.data()), s.size());
}

void write_status_impl(BridgeStatus &status, StatusCode code, StatusSubCode subcode, StatusSeverity severity,
                       int bridge_code);

inline void write_status(Status &&rstatus, BridgeStatus &status) {
    if (rstatus.code() != StatusCode::kOk || rstatus.subcode() != StatusSubCode::kNoSpace ||
        rstatus.severity() != StatusSeverity::kNoError) {
        write_status_impl(status, rstatus.code(), rstatus.subcode(), rstatus.severity(), 0);
    }
}

void set_verify_checksums(ReadOptions &options, const bool v) {
    options.verify_checksums = v;
}

void set_total_order_seek(ReadOptions &options, const bool v) {
    options.total_order_seek = v;
}


void set_disable_wal(WriteOptions &options, const bool v) {
    options.disableWAL = v;
}


typedef rust::Fn<std::int8_t(rust::Slice<const std::uint8_t>, rust::Slice<const std::uint8_t>)> RustComparatorFn;

class RustComparator : public Comparator {
public:
    inline int Compare(const rocksdb::Slice &a, const rocksdb::Slice &b) const {
        return int(rust_compare(convert_slice_back(a), convert_slice_back(b)));
    }

    const char *Name() const {
        return name.c_str();
    }

    virtual bool CanKeysWithDifferentByteContentsBeEqual() const {
        return can_different_bytes_be_equal;
    }

    void FindShortestSeparator(std::string *, const rocksdb::Slice &) const {}

    void FindShortSuccessor(std::string *) const {}

    void set_fn(RustComparatorFn f) {
        rust_compare = f;
    }

    void set_name(rust::Str name_) {
        name = std::string(name_);
    }

    void set_can_different_bytes_be_equal(bool v) {
        can_different_bytes_be_equal = v;
    }

    std::string name;
    RustComparatorFn rust_compare;
    bool can_different_bytes_be_equal;
};

inline unique_ptr<RustComparator> new_rust_comparator(rust::Str name, RustComparatorFn f, bool diff_bytes_can_equal) {
    auto ret = make_unique<RustComparator>();
    ret->set_name(name);
    ret->set_fn(f);
    ret->set_can_different_bytes_be_equal(diff_bytes_can_equal);
    return ret;
}


inline void prepare_for_bulk_load(Options &inner) {
    inner.PrepareForBulkLoad();
}

inline void increase_parallelism(Options &inner) {
    inner.IncreaseParallelism();
}

inline void optimize_level_style_compaction(Options &inner) {
    inner.OptimizeLevelStyleCompaction();
};

inline void set_create_if_missing(Options &inner, bool v) {
    inner.create_if_missing = v;
}

inline void set_comparator(Options &inner, const RustComparator &cmp_obj) {
    inner.comparator = &cmp_obj;
}

inline void set_paranoid_checks(Options &inner, bool v) {
    inner.paranoid_checks = v;
}

inline std::unique_ptr<ReadOptions> new_read_options() {
    return std::make_unique<ReadOptions>();
}

inline std::unique_ptr<WriteOptions> new_write_options() {
    return std::make_unique<WriteOptions>();
}

inline std::unique_ptr<Options> new_options() {
    return std::make_unique<Options>();
}

struct IteratorBridge {
    mutable std::unique_ptr<Iterator> inner;

    IteratorBridge(Iterator *it) : inner(it) {}

    inline void seek_to_first() const {
        inner->SeekToFirst();
    }

    inline void seek_to_last() const {
        inner->SeekToLast();
    }

    inline void next() const {
        inner->Next();
    }

    inline bool is_valid() const {
        return inner->Valid();
    }

    inline void do_seek(rust::Slice<const uint8_t> key) const {
        auto k = Slice(reinterpret_cast<const char *>(key.data()), key.size());
        inner->Seek(k);
    }

    inline void do_seek_for_prev(rust::Slice<const uint8_t> key) const {
        auto k = Slice(reinterpret_cast<const char *>(key.data()), key.size());
        inner->SeekForPrev(k);
    }

    inline std::unique_ptr<Slice> key_raw() const {
//        std::cout << "c++ get  " << inner->key().size() << std::endl;
        return std::make_unique<Slice>(inner->key());
    }

    inline std::unique_ptr<Slice> value_raw() const {
        return std::make_unique<Slice>(inner->value());
    }

    BridgeStatus status() const;
};


inline unique_ptr<TransactionOptions> new_transaction_options() {
    return make_unique<TransactionOptions>();
}

inline void set_deadlock_detect(TransactionOptions &inner, bool v) {
    inner.deadlock_detect = v;
}

inline unique_ptr<OptimisticTransactionOptions> new_optimistic_transaction_options(const RustComparator &compare) {
    auto ret = make_unique<OptimisticTransactionOptions>();
    ret->cmp = &compare;
    return ret;
}

struct TransactionBridge {
    DB *raw_db;
    unique_ptr<Transaction> inner;
    mutable unique_ptr<TransactionOptions> t_ops; // Put here to make sure ownership works
    mutable unique_ptr<OptimisticTransactionOptions> o_ops; // same as above
    mutable unique_ptr<ReadOptions> r_ops;
    mutable unique_ptr<ReadOptions> raw_r_ops;
    mutable unique_ptr<WriteOptions> w_ops;
    mutable unique_ptr<WriteOptions> raw_w_ops;

    inline void set_snapshot() const {
        inner->SetSnapshot();
        r_ops->snapshot = inner->GetSnapshot();
    }

    inline void commit(BridgeStatus &status) const {
        write_status(inner->Commit(), status);
        r_ops->snapshot = nullptr;
    }

    inline void rollback(BridgeStatus &status) const {
        write_status(inner->Rollback(), status);
    }

    inline void set_savepoint() const {
        inner->SetSavePoint();
    }

    inline void rollback_to_savepoint(BridgeStatus &status) const {
        write_status(inner->RollbackToSavePoint(), status);
    }

    inline void pop_savepoint(BridgeStatus &status) const {
        write_status(inner->PopSavePoint(), status);
    }

    unique_ptr<vector<PinnableSlice>> multiget_txn(
            const ColumnFamilyHandle &cf,
            rust::Slice<const rust::Slice<const uint8_t>> keys,
            rust::Slice<BridgeStatus> statuses) const;

    unique_ptr<vector<PinnableSlice>> multiget_raw(
            const ColumnFamilyHandle &cf,
            rust::Slice<const rust::Slice<const uint8_t>> keys,
            rust::Slice<BridgeStatus> statuses) const;


    inline unique_ptr<PinnableSlice> get_txn(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            BridgeStatus &status
    ) const {
        auto pinnable_val = std::make_unique<PinnableSlice>();
        write_status(
                inner->Get(*r_ops,
                           const_cast<ColumnFamilyHandle *>(&cf),
                           convert_slice(key),
                           &*pinnable_val),
                status
        );
        return pinnable_val;
    }

    inline unique_ptr<PinnableSlice> get_for_update_txn(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            BridgeStatus &status
    ) const {
        auto pinnable_val = std::make_unique<PinnableSlice>();
        write_status(
                inner->GetForUpdate(*r_ops,
                                    const_cast<ColumnFamilyHandle *>(&cf),
                                    convert_slice(key),
                                    &*pinnable_val),
                status
        );
        return pinnable_val;
    }

    inline std::unique_ptr<PinnableSlice> get_raw(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            BridgeStatus &status
    ) const {
        auto pinnable_val = std::make_unique<PinnableSlice>();
        write_status(
                raw_db->Get(*r_ops,
                            const_cast<ColumnFamilyHandle *>(&cf),
                            convert_slice(key),
                            &*pinnable_val),
                status
        );
        return pinnable_val;
    }

    inline void put_txn(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            rust::Slice<const uint8_t> val,
            BridgeStatus &status
    ) const {
        write_status(
                inner->Put(const_cast<ColumnFamilyHandle *>(&cf),
                           convert_slice(key),
                           convert_slice(val)),
                status
        );
    }

    inline void put_raw(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            rust::Slice<const uint8_t> val,
            BridgeStatus &status
    ) const {
        auto k = convert_slice(key);
        auto v = convert_slice(val);
//        std::cout << "c++ put  " << key.size() << "  " << k.size() << std::endl;
        write_status(
                raw_db->Put(
                        *raw_w_ops,
                        const_cast<ColumnFamilyHandle *>(&cf),
                        k,
                        v),
                status
        );
    }

    inline void del_txn(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            BridgeStatus &status
    ) const {
        write_status(
                inner->Delete(const_cast<ColumnFamilyHandle *>(&cf),
                              convert_slice(key)),
                status
        );
    }

    inline void del_raw(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> key,
            BridgeStatus &status
    ) const {
        write_status(
                raw_db->Delete(
                        *raw_w_ops,
                        const_cast<ColumnFamilyHandle *>(&cf),
                        convert_slice(key)),
                status
        );
    }

    inline void del_range_raw(
            const ColumnFamilyHandle &cf,
            rust::Slice<const uint8_t> start_key,
            rust::Slice<const uint8_t> end_key,
            BridgeStatus &status
    ) const {
        write_status(
                raw_db->GetRootDB()->DeleteRange(
                        *raw_w_ops, const_cast<ColumnFamilyHandle *>(&cf),
                        convert_slice(start_key), convert_slice(end_key)),
                status);
    }

    inline void flush_raw(const ColumnFamilyHandle &cf, const FlushOptions &options, BridgeStatus &status) const {
        write_status(raw_db->Flush(options, const_cast<ColumnFamilyHandle *>(&cf)), status);
    }


    inline std::unique_ptr<IteratorBridge> iterator_txn(
            const ColumnFamilyHandle &cf) const {
        return std::make_unique<IteratorBridge>(
                inner->GetIterator(*r_ops, const_cast<ColumnFamilyHandle *>(&cf)));
    }

    inline std::unique_ptr<IteratorBridge> iterator_raw(
            const ColumnFamilyHandle &cf) const {
        return std::make_unique<IteratorBridge>(
                raw_db->NewIterator(*raw_r_ops, const_cast<ColumnFamilyHandle *>(&cf)));
    }
};

inline tuple<vector<string>, vector<ColumnFamilyDescriptor>>
get_cf_data(const Options &options,
            const string &path) {
    auto cf_names = std::vector<std::string>();
    DB::ListColumnFamilies(options, path, &cf_names);
    if (cf_names.empty()) {
        cf_names.push_back(kDefaultColumnFamilyName);
    }

    std::vector<ColumnFamilyDescriptor> column_families;

    for (auto el: cf_names) {
        column_families.push_back(ColumnFamilyDescriptor(
                el, options));
    }
    return std::make_tuple(cf_names, column_families);
}

struct TDBBridge {
    mutable unique_ptr<StackableDB> db;
    mutable TransactionDB *tdb;
    mutable OptimisticTransactionDB *odb;
    mutable unordered_map<string, shared_ptr<ColumnFamilyHandle>> handles;
    mutable Lock handle_lock;
    bool is_odb;

    TDBBridge(StackableDB *db_,
              TransactionDB *tdb_,
              OptimisticTransactionDB *odb_,
              unordered_map<string, shared_ptr<ColumnFamilyHandle>> &&handles_) :
            db(db_), tdb(tdb_), odb(odb_), handles(handles_), handle_lock() {
        is_odb = (tdb_ == nullptr);
    }

    inline unique_ptr<TransactionBridge> begin_t_transaction(
            unique_ptr<WriteOptions> w_ops,
            unique_ptr<WriteOptions> raw_w_ops,
            unique_ptr<ReadOptions> r_ops,
            unique_ptr<ReadOptions> raw_r_ops,
            unique_ptr<TransactionOptions> txn_options) const {
        if (tdb == nullptr) {
            return unique_ptr<TransactionBridge>(nullptr);
        }
        auto ret = make_unique<TransactionBridge>();
        ret->raw_db = tdb;
        ret->r_ops = std::move(r_ops);
        ret->w_ops = std::move(w_ops);
        ret->raw_r_ops = std::move(raw_r_ops);
        ret->raw_w_ops = std::move(raw_w_ops);
        ret->t_ops = std::move(txn_options);
        Transaction *txn = tdb->BeginTransaction(*ret->w_ops, *ret->t_ops);
        ret->inner = unique_ptr<Transaction>(txn);
        return ret;
    }

    inline unique_ptr<TransactionBridge> begin_o_transaction(
            unique_ptr<WriteOptions> w_ops,
            unique_ptr<WriteOptions> raw_w_ops,
            unique_ptr<ReadOptions> r_ops,
            unique_ptr<ReadOptions> raw_r_ops,
            unique_ptr<OptimisticTransactionOptions> txn_options) const {
        if (odb == nullptr) {
            return unique_ptr<TransactionBridge>(nullptr);
        }
        auto ret = make_unique<TransactionBridge>();
        ret->raw_db = odb;
        ret->r_ops = std::move(r_ops);
        ret->w_ops = std::move(w_ops);
        ret->raw_r_ops = std::move(raw_r_ops);
        ret->raw_w_ops = std::move(raw_w_ops);
        ret->o_ops = std::move(txn_options);
        Transaction *txn = odb->BeginTransaction(*ret->w_ops, *ret->o_ops);
        ret->inner = unique_ptr<Transaction>(txn);
        return ret;
    }

    inline shared_ptr<ColumnFamilyHandle> get_cf_handle_raw(const string &name) const {
        ReadLock r_lock(handle_lock);
        try {
            return handles.at(name);
        } catch (const std::out_of_range &) {
            return shared_ptr<ColumnFamilyHandle>(nullptr);
        }
    }

    inline shared_ptr<ColumnFamilyHandle> get_default_cf_handle_raw() const {
        return handles.at("default");
    }

    inline shared_ptr<ColumnFamilyHandle>
    create_column_family_raw(const Options &options, const string &name, BridgeStatus &status) const {
        {
            ReadLock r_lock(handle_lock);
            if (handles.find(name) != handles.end()) {
                write_status_impl(status, StatusCode::kMaxCode, StatusSubCode::kMaxSubCode,
                                  StatusSeverity::kSoftError,
                                  2);
                return shared_ptr<ColumnFamilyHandle>(nullptr);
            }
        }
        {
            WriteLock w_lock(handle_lock);
            ColumnFamilyHandle *handle;
            auto s = db->CreateColumnFamily(options, name, &handle);
            write_status(std::move(s), status);
            auto ret = shared_ptr<ColumnFamilyHandle>(handle);
            handles[name] = ret;
            return ret;
        }
    }

    inline void drop_column_family_raw(const string &name, BridgeStatus &status) const {
        WriteLock w_lock(handle_lock);
        auto cf_it = handles.find(name);
        if (cf_it != handles.end()) {
            auto s = db->DropColumnFamily(cf_it->second.get());
            handles.erase(cf_it);
            write_status(std::move(s), status);
        } else {
            write_status_impl(status, StatusCode::kMaxCode, StatusSubCode::kMaxSubCode, StatusSeverity::kSoftError,
                              3);
        }
        // When should we call DestroyColumnFamilyHandle?
    }

    inline unique_ptr<vector<string>> get_column_family_names_raw() const {
        ReadLock r_lock(handle_lock);
        auto ret = make_unique<vector<string >>();
        for (auto entry: handles) {
            ret->push_back(entry.first);
        }
        return ret;
    }
};

inline unique_ptr<TransactionDBOptions> new_tdb_options() {
    return make_unique<TransactionDBOptions>();
}

inline unique_ptr<OptimisticTransactionDBOptions> new_odb_options() {
    return make_unique<OptimisticTransactionDBOptions>();
}

inline unique_ptr<FlushOptions> new_flush_options() {
    return make_unique<FlushOptions>();
}

void set_flush_wait(FlushOptions &options, bool v) {
    options.wait = v;
}

void set_allow_write_stall(FlushOptions &options, bool v) {
    options.allow_write_stall = v;
}

inline unique_ptr<TDBBridge>
open_tdb_raw(const Options &options,
             const TransactionDBOptions &txn_db_options,
             const string &path,
             BridgeStatus &status) {
    auto cf_info = get_cf_data(options, path);
    auto cf_names = std::get<0>(cf_info);
    auto column_families = std::get<1>(cf_info);


    std::vector<ColumnFamilyHandle *> handles;
    TransactionDB *txn_db = nullptr;

    Status s = TransactionDB::Open(options, txn_db_options, path,
                                   column_families, &handles,
                                   &txn_db);
    auto ok = s.ok();
    write_status(std::move(s), status);

    unordered_map<string, shared_ptr<ColumnFamilyHandle>> handle_map;
    if (ok) {
        assert(handles.size() == cf_names.size());
        for (size_t i = 0; i < handles.size(); ++i) {
            handle_map[cf_names[i]] = shared_ptr<ColumnFamilyHandle>(handles[i]);
        }
    }

    return make_unique<TDBBridge>(txn_db, txn_db, nullptr, std::move(handle_map));
}


inline unique_ptr<TDBBridge>
open_odb_raw(const Options &options,
             const OptimisticTransactionDBOptions &txn_db_options,
             const string &path,
             BridgeStatus &status) {
    auto cf_info = get_cf_data(options, path);
    auto cf_names = std::get<0>(cf_info);
    auto column_families = std::get<1>(cf_info);


    std::vector<ColumnFamilyHandle *> handles;
    OptimisticTransactionDB *txn_db = nullptr;

    Status s = OptimisticTransactionDB::Open(options, txn_db_options, path,
                                             column_families, &handles,
                                             &txn_db);
    auto ok = s.ok();
    write_status(std::move(s), status);


    unordered_map<string, shared_ptr<ColumnFamilyHandle>> handle_map;
    if (ok) {
        assert(handles.size() == cf_names.size());
        for (size_t i = 0; i < handles.size(); ++i) {
            handle_map[cf_names[i]] = shared_ptr<ColumnFamilyHandle>(handles[i]);
        }
    }

    return make_unique<TDBBridge>(txn_db, nullptr, txn_db, std::move(handle_map));
}