
#pragma once

#include <llvm/IR/IRBuilder.h>

#include "Common.h"
#include "BasicBlock.h"

namespace dev
{
namespace eth
{
namespace jit
{

class Compiler
{
public:

	struct Options
	{
		/// Optimize stack operations between basic blocks
		bool optimizeStack;

		/// Rewrite switch instructions to sequences of branches
		bool rewriteSwitchToBranches;

		/// Dump CFG as a .dot file for graphviz
		bool dumpCFG;

		Options():
			optimizeStack(true),
			rewriteSwitchToBranches(true),
			dumpCFG(false)
		{}
	};

	using ProgramCounter = uint64_t;

	Compiler(Options const& _options);

	std::unique_ptr<llvm::Module> compile(bytes const& _bytecode);

private:

	void createBasicBlocks(bytes const& _bytecode);

	void compileBasicBlock(BasicBlock& _basicBlock, bytes const& _bytecode, class RuntimeManager& _runtimeManager, class Arith256& _arith, class Memory& _memory, class Ext& _ext, class GasMeter& _gasMeter, llvm::BasicBlock* _nextBasicBlock);

	void removeDeadBlocks();

	/// Dumps basic block graph in graphviz format to a file, if option dumpCFG is enabled.
	void dumpCFGifRequired(std::string const& _dotfilePath);

	/// Dumps basic block graph in graphviz format to a stream.
	void dumpCFGtoStream(std::ostream& _out);

	/// Dumps all basic blocks to stderr. Useful in a debugging session.
	void dump();

	/// Compiler options
	Options const& m_options;

	/// Helper class for generating IR
	llvm::IRBuilder<> m_builder;

	/// Maps a program counter pc to a basic block that starts at pc (if any).
	std::map<ProgramCounter, BasicBlock> basicBlocks = {};

	/// Maps a pc at which there is a JUMP or JUMPI to the target block of the jump.
	std::map<ProgramCounter, llvm::BasicBlock*> m_directJumpTargets = {};

	/// A list of possible blocks to which there may be indirect jumps.
	std::vector<BasicBlock*> m_indirectJumpTargets = {};

	/// Stop basic block - terminates execution with STOP code (0)
	llvm::BasicBlock* m_stopBB = nullptr;

	/// Block with a jump table.
	std::unique_ptr<BasicBlock> m_jumpTableBlock = nullptr;

	/// Default destination for indirect jumps.
	std::unique_ptr<BasicBlock> m_badJumpBlock = nullptr;

	/// Main program function
	llvm::Function* m_mainFunc = nullptr;
};

}
}
}
