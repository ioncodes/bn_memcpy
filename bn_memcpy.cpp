#include <optional>
#include <vector>
#include <array>

#include <binaryninjaapi.h>
#include <highlevelilinstruction.h>

using namespace BinaryNinja;

BN_DECLARE_CORE_ABI_VERSION

struct CopyLoopInformation
{
    HighLevelILInstruction head;
    HighLevelILInstruction body;
    std::pair<Variable, Variable> variables;
    int64_t size;
};

// Detect *(x + i) = *(y + i) patterns
std::optional<CopyLoopInformation> DetectDerefCopyLoop(Ref<HighLevelILFunction> function,
    const HighLevelILInstructionAccessor<HLIL_ASSIGN>& assignment, size_t idx, int64_t size)
{
    // Is this an indexed operation?
    if (assignment.GetDestExpr().operation != HLIL_DEREF ||
        assignment.GetSourceExpr().operation != HLIL_DEREF)
        return std::nullopt;

    // Is it an added operation?
    const auto& lhs = assignment.GetDestExpr().As<HLIL_DEREF>().GetSourceExpr();
    const auto& rhs = assignment.GetSourceExpr().As<HLIL_DEREF>().GetSourceExpr();
    if (lhs.operation != HLIL_ADD ||
        rhs.operation != HLIL_ADD)
        return std::nullopt;

    // We found a *(lhs + i) = *(rhs + i) pattern!
    if (lhs.GetLeftExpr().operation != HLIL_VAR ||
        rhs.GetLeftExpr().operation != HLIL_VAR)
        return std::nullopt;

    const auto dst = lhs.GetLeftExpr().As<HLIL_VAR>().GetVariable();
    const auto src = rhs.GetLeftExpr().As<HLIL_VAR>().GetVariable();

    return CopyLoopInformation
    {
        .head = function->GetInstruction(idx - 1),
        .body = function->GetInstruction(idx),
        .variables = { dst, src },
        .size = size
    };
}

// Detect x[i] = y[i] patterns
std::optional<CopyLoopInformation> DetectIndexedCopyLoop(Ref<HighLevelILFunction> function,
    const HighLevelILInstructionAccessor<HLIL_ASSIGN>& assignment, size_t idx, int64_t size)
{
    // Is this an indexed operation?
    if (assignment.GetDestExpr().operation != HLIL_ARRAY_INDEX ||
        assignment.GetSourceExpr().operation != HLIL_ARRAY_INDEX)
        return std::nullopt;

    // Does assignment use variables on both sides?
    const auto& lhs = assignment.GetDestExpr().As<HLIL_ARRAY_INDEX>().GetSourceExpr();
    const auto& rhs = assignment.GetSourceExpr().As<HLIL_ARRAY_INDEX>().GetSourceExpr();
    if (lhs.operation != HLIL_VAR ||
        rhs.operation != HLIL_VAR)
        return std::nullopt;

    // We found a lhs[i] = rhs[i] pattern!
    const auto dst = lhs.As<HLIL_VAR>().GetVariable();
    const auto src = rhs.As<HLIL_VAR>().GetVariable();

    return CopyLoopInformation
    {
        .head = function->GetInstruction(idx - 1),
        .body = function->GetInstruction(idx),
        .variables = { dst, src },
        .size = size
    };
}

std::vector<CopyLoopInformation> DetectCopyLoop(Ref<HighLevelILFunction> function)
{
    std::vector<CopyLoopInformation> loops;

    for (size_t i = 0; i < function->GetInstructionCount(); i++)
    {
        try
        {
            // Is it a while loop?
            auto instruction = function->GetInstruction(i);
            if (instruction.operation != HLIL_WHILE)
                continue;

            // Is it a HLIL_CMP_SLT conditional?
            // Does it have a constant size?
            const auto condition = instruction.As<HLIL_WHILE>().GetConditionExpr();
            if (condition.operation != HLIL_CMP_SLT ||
                condition.GetRightExpr().operation != HLIL_CONST)
                continue;

            // Get the loop length
            const auto size = condition.GetRightExpr().As<HLIL_CONST>().GetConstant();
        
            // Is the inner loop body an assignment instruction?
            instruction = function->GetInstruction(++i);
            if (instruction.operation != HLIL_ASSIGN)
                continue;

            // Cast to an assignment operation
            const auto& assignment = instruction.As<HLIL_ASSIGN>();

            // Pattern match
            auto opt = DetectIndexedCopyLoop(function, assignment, i, size);
            if (!opt.has_value())
                opt = DetectDerefCopyLoop(function, assignment, i, size);

            if (opt.has_value())
                loops.push_back(std::move(opt.value()));
        }
        catch (const std::exception& exception)
        {
            LogError("Error: %s\n", exception.what());
        }
    }

    return loops;
}

void SimplifyMemcpy(Ref<AnalysisContext> analysisContext)
{
    const auto hlilFunction = analysisContext->GetHighLevelILFunction();
    const auto function = analysisContext->GetFunction();
    const auto loops = DetectCopyLoop(hlilFunction);

    for (const auto& loop : loops)
    {
        const auto& [dst, src] = loop.variables;

        const auto& dst_name = function->GetVariableName(dst);
        const auto& src_name = function->GetVariableName(src);

        LogInfo("Found memcpy(%s, %s, 0x%llx)\n", dst_name, src_name, loop.size);

        const auto symbols = function->GetView()->GetSymbolsByName("memcpy");
        const auto memcpy = hlilFunction->AddExpr(HLIL_CONST_PTR, 8, symbols[0]->GetAddress());
        const auto memcpy_args = hlilFunction->AddOperandList({
            hlilFunction->AddExpr(HLIL_VAR, 8, dst.ToIdentifier()),
            hlilFunction->AddExpr(HLIL_VAR, 8, src.ToIdentifier()),
            hlilFunction->AddExpr(HLIL_CONST, 8, loop.size)
        });
        const auto memcpy_call = hlilFunction->AddExpr(HLIL_CALL, 8, memcpy, 3, memcpy_args);
        const auto nop = hlilFunction->AddExpr(HLIL_NOP, 0);

        hlilFunction->ReplaceExpr(loop.head.exprIndex, memcpy_call);
        hlilFunction->ReplaceExpr(loop.body.exprIndex, nop);

        hlilFunction->Finalize();
    }
}

extern "C" BINARYNINJAPLUGIN bool CorePluginInit()
{
    const auto workflow = Workflow::Instance()->Clone("extension.memcpy");
    workflow->RegisterActivity(new Activity("extension.memcpy", &SimplifyMemcpy));
    // Insert after generateHighLevelIL
    workflow->Insert("core.function.commitAnalysisData", "extension.memcpy");
    Workflow::RegisterWorkflow(workflow);

    return true;
}