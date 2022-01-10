#include <vector>
#include "binaryninjaapi.h"
#include "highlevelilinstruction.h"

using namespace BinaryNinja;

BN_DECLARE_CORE_ABI_VERSION

void SimplifyMemcpy(Ref<AnalysisContext> analysisContext)
{
    const auto hlilFunction = analysisContext->GetHighLevelILFunction();
    const auto function = analysisContext->GetFunction();

    for (size_t i = 0; i < hlilFunction->GetInstructionCount(); i++)
    {
        const auto loop_instruction = hlilFunction->GetInstruction(i);
        if (loop_instruction.operation != HLIL_WHILE)
            continue;
       
        const auto& loop = loop_instruction.As<HLIL_WHILE>();
        const auto cond = loop.GetConditionExpr();

        try
        {
            const auto size = cond
                .GetRightExpr().As<HLIL_CONST>()
                .GetConstant();

            const auto assign_instruction = hlilFunction->GetInstruction(i + 1);
            const auto& assign = assign_instruction.As<HLIL_ASSIGN>();
            const auto& dst = assign
                .GetDestExpr().As<HLIL_ARRAY_INDEX>()
                .GetSourceExpr().As<HLIL_VAR>()
                .GetVariable();
                
            const auto& src = assign
                .GetSourceExpr().As<HLIL_ARRAY_INDEX>()
                .GetSourceExpr().As<HLIL_VAR>()
                .GetVariable();

            const auto& dst_name = function->GetVariableName(dst);
            const auto& src_name = function->GetVariableName(src);

            LogInfo("Found memcpy(%s, %s, 0x%llx)\n", dst_name, src_name, size);

            const auto symbols = function->GetView()->GetSymbolsByName("memcpy");
            const auto memcpy = hlilFunction->AddExpr(HLIL_CONST_PTR, 8, symbols[0]->GetAddress());
            const auto memcpy_args = hlilFunction->AddOperandList({
                hlilFunction->AddExpr(HLIL_VAR, 8, dst.ToIdentifier()),
                hlilFunction->AddExpr(HLIL_VAR, 8, src.ToIdentifier()),
                hlilFunction->AddExpr(HLIL_CONST, 8, size)
            });
            const auto memcpy_call = hlilFunction->AddExpr(
                HLIL_CALL, 8, memcpy, 3, memcpy_args);
            const auto nop = hlilFunction->AddExpr(HLIL_NOP, 0);

            hlilFunction->ReplaceExpr(loop_instruction.exprIndex, memcpy_call);
            hlilFunction->ReplaceExpr(assign_instruction.exprIndex, nop);

            hlilFunction->Finalize();

            i++;
        }
        catch(...) {}
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