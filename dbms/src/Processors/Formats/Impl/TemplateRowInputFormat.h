#pragma once

#include <Core/Block.h>
#include <Processors/Formats/RowInputFormatWithDiagnosticInfo.h>
#include <Formats/FormatSettings.h>
#include <Processors/Formats/Impl/TemplateBlockOutputFormat.h>
#include <IO/ReadHelpers.h>
#include <IO/PeekableReadBuffer.h>


namespace DB
{

class TemplateRowInputFormat : public RowInputFormatWithDiagnosticInfo
{
    using ColumnFormat = ParsedTemplateFormatString::ColumnFormat;
public:
    TemplateRowInputFormat(const Block & header_, ReadBuffer & in_, const Params & params_,
                           const FormatSettings & settings_, bool ignore_spaces_);

    String getName() const override { return "TemplateRowInputFormat"; }

    bool readRow(MutableColumns & columns, RowReadExtension & extra) override;

    void readPrefix() override;

    bool allowSyncAfterError() const override;
    void syncAfterError() override;

private:
    void deserializeField(const IDataType & type, IColumn & column, ColumnFormat col_format);
    void skipField(ColumnFormat col_format);
    inline void skipSpaces() { if (ignore_spaces) skipWhitespaceIfAny(buf); }

    template <typename ReturnType = void>
    ReturnType tryReadPrefixOrSuffix(size_t & input_part_beg, size_t input_part_end);
    bool checkForSuffix();
    [[noreturn]] void throwUnexpectedEof();

    bool parseRowAndPrintDiagnosticInfo(MutableColumns & columns, WriteBuffer & out) override;
    void tryDeserializeFiled(const DataTypePtr & type, IColumn & column, size_t file_column, ReadBuffer::Position & prev_pos,
                             ReadBuffer::Position & curr_pos) override;
    bool isGarbageAfterField(size_t after_col_idx, ReadBuffer::Position pos) override;
    void writeErrorStringForWrongDelimiter(WriteBuffer & out, const String & description, const String & delim);

    void skipToNextDelimiterOrEof(const String & delimiter);

private:
    PeekableReadBuffer buf;
    DataTypes data_types;

    FormatSettings settings;
    ParsedTemplateFormatString format;
    ParsedTemplateFormatString row_format;
    const bool ignore_spaces;

    size_t format_data_idx;
    bool end_of_stream = false;
};

}