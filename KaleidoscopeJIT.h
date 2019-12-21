#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

#include <memory>

namespace llvm {
namespace orc {
	class KaleidoscopeJIT
	{
	private:
		ExecutionSession ES;
		RTDyldObjectLinkingLayer ObjectLayer;
		IRCompileLayer CompileLayer;
		IRTransformLayer OptimizeLayer;

		DataLayout DL;
		MangleAndInterner Mangle;
		ThreadSafeContext Ctx;

		JITDylib &MainJD;

	public:
		KaleidoscopeJIT(JITTargetMachineBuilder JTMB, DataLayout DL)
			:ObjectLayer(ES, [](){ return std::make_unique<SectionMemoryManager>();}),
			 CompileLayer(ES, ObjectLayer, ConcurrentIRCompiler(std::move(JTMB))),
			 OptimizeLayer(ES, CompileLayer, optimizeModule),
			 DL(std::move(DL)),
			 Mangle(ES, this->DL),
			 Ctx(std::make_unique<LLVMContext>()),
			 MainJD(ES.createJITDylib("<main>"))
		{
			 MainJD.addGenerator(
			 	cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
					DL.getGlobalPrefix())));
		}
		
		static Expected<std::unique_ptr<KaleidoscopeJIT>> Create()
		{
			auto JTMB = JITTargetMachineBuilder::detectHost();
			if (!JTMB) {
				return JTMB.takeError();
			}

			auto DL = JTMB->getDefaultDataLayoutForTarget();
			if (!DL) {
				return DL.takeError();
			}
			
			return std::make_unique<KaleidoscopeJIT>(std::move(*JTMB), std::move(*DL));
		}

		const DataLayout &getDataLayout() const
		{
			return DL;
		}

		LLVMContext &getContext()
		{
			return *Ctx.getContext();
		}

		Error addModule(std::unique_ptr<Module> M)
		{
			return OptimizeLayer.add(MainJD, ThreadSafeModule(std::move(M), Ctx));
		}

		Expected<JITEvaluatedSymbol> lookup(StringRef Name)
		{
			return ES.lookup({&MainJD}, Mangle(Name.str()));
		}
	
	private:
		static Expected<ThreadSafeModule>
		optimizeModule(ThreadSafeModule TSM, const MaterializationResponsibility &R)
		{
			TSM.withModuleDo([](Module &M){
				// Create a function pass manager.
				auto FPM = std::make_unique<legacy::FunctionPassManager>(&M);				
				
				FPM->add(createInstructionCombiningPass());
				FPM->add(createReassociatePass());
				FPM->add(createGVNPass());
				FPM->add(createCFGSimplificationPass());
				
				FPM->doInitialization();
				
				// Run the optimizations over all functions in the module being added to
				// the JIT.	
				for (auto &F : M) {
					FPM->run(F);
				}
			});
			return TSM;
		}
	};
} //end namespace orc
} //end namespace llvm

