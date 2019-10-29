#include <Functions/IFunction.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <DataTypes/DataTypeString.h>
#include <Columns/ColumnString.h>
#include <Interpreters/Context.h>
#include <Common/Macros.h>
#include <Core/Field.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int ILLEGAL_COLUMN;
}

/** Get scalar value of sub queries from query context via IAST::Hash.
  */
class FunctionGetScalar : public IFunction
{
public:
    static constexpr auto name = "__getScalar";
    static FunctionPtr create(const Context & context)
    {
        return std::make_shared<FunctionGetScalar>(context);
    }

    FunctionGetScalar(const Context & context_) : context(context_) {}

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override
    {
        return 1;
    }

    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override
    {
        if (arguments.size() != 1 || !isString(arguments[0].type) || !isColumnConst(*arguments[0].column))
            throw Exception("Function " + getName() + " accepts one const string argument", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        auto scalar_name = assert_cast<const ColumnConst &>(*arguments[0].column).getField().get<String>();
        scalar = context.getScalar(scalar_name).getByPosition(0);
        return scalar.type;
    }

    void executeImpl(Block & block, const ColumnNumbers &, size_t result, size_t input_rows_count) override
    {
        block.getByPosition(result).column = ColumnConst::create(scalar.column, input_rows_count);
    }

private:
    mutable ColumnWithTypeAndName scalar;
    const Context & context;
};


void registerFunctionGetScalar(FunctionFactory & factory)
{
    factory.registerFunction<FunctionGetScalar>();
}

}
