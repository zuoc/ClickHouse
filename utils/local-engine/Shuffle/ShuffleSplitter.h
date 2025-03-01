#pragma once
#include <Core/Block.h>
#include <Columns/IColumn.h>
#include <Common/PODArray_fwd.h>
#include <Common/PODArray.h>
#include <Formats/NativeWriter.h>
#include <IO/WriteBufferFromFile.h>
#include <Functions/IFunction.h>



//using namespace DB;

namespace local_engine
{

struct SplitOptions
{
    size_t buffer_size = DEFAULT_BLOCK_SIZE;
    std::string data_file;
    std::string local_tmp_dir;
    int map_id;
    size_t partition_nums;
    std::vector<std::string> exprs;
    std::string compress_method = "zstd";
    int compress_level;
};

class ColumnsBuffer
{
public:
    explicit ColumnsBuffer(size_t prefer_buffer_size=8192);
    void add(DB::Block & columns, int start, int end);
    size_t size() const;
    DB::Block releaseColumns();
    DB::Block getHeader();

private:
    DB::MutableColumns accumulated_columns;
    DB::Block header;
    size_t prefer_buffer_size;
};

struct SplitResult
{
    Int64 total_compute_pid_time = 0;
    Int64 total_write_time = 0;
    Int64 total_spill_time = 0;
    Int64 total_bytes_written = 0;
    Int64 total_bytes_spilled = 0;
    std::vector<Int64> partition_length;
    std::vector<Int64> raw_partition_length;
};

class ShuffleSplitter
{
public:
    static const std::vector<std::string> compress_methods;
    using Ptr = std::unique_ptr<ShuffleSplitter>;
    static Ptr create(std::string short_name, SplitOptions options_);
    explicit ShuffleSplitter(SplitOptions && options);
    virtual ~ShuffleSplitter()
    {
        if (!stopped) stop();
    }
    void split(DB::Block& block);
    virtual void computeAndCountPartitionId(DB::Block & block) {}
    std::vector<int64_t> getPartitionLength() {
        return split_result.partition_length;
    }
    void writeIndexFile();
    SplitResult stop();

private:
    void init();
    void splitBlockByPartition(DB::Block & block);
    void buildSelector(size_t row_nums, DB::IColumn::Selector & selector);
    void spillPartition(size_t partition_id);
    std::string getPartitionTempFile(size_t partition_id);
    void mergePartitionFiles();
    std::unique_ptr<DB::WriteBuffer> getPartitionWriteBuffer(size_t partition_id);

protected:
    bool stopped = false;
    std::vector<DB::IColumn::ColumnIndex> partition_ids;
    std::vector<ColumnsBuffer> partition_buffer;
    std::vector<std::unique_ptr<DB::NativeWriter>> partition_outputs;
    std::vector<std::unique_ptr<DB::WriteBuffer>> partition_write_buffers;
    std::vector<std::unique_ptr<DB::WriteBuffer>> partition_cached_write_buffers;
    SplitOptions options;
    SplitResult split_result;
};

class RoundRobinSplitter : public ShuffleSplitter {
public:
    static std::unique_ptr<ShuffleSplitter> create(SplitOptions && options);

    RoundRobinSplitter(SplitOptions options_)
        : ShuffleSplitter(std::move(options_)) {}

    ~RoundRobinSplitter() override = default;
    void computeAndCountPartitionId(DB::Block & block) override;

private:
    int32_t pid_selection_ = 0;
};

class HashSplitter : public ShuffleSplitter {
public:
    static std::unique_ptr<ShuffleSplitter> create(SplitOptions && options);

    HashSplitter(SplitOptions options_)
        : ShuffleSplitter(std::move(options_)) {}

    ~HashSplitter() override = default;
    void computeAndCountPartitionId(DB::Block & block) override;

private:
    DB::FunctionBasePtr hash_function;
};

struct SplitterHolder
{
    ShuffleSplitter::Ptr splitter;
};


}
