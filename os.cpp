#include <stdexcept>
#include <string>
#include <string_view>

#include <cstdint>
#include <cstdlib>

#include "config.h"
#include "lib.h"
#include "arq-sim.h"
#include "os.h"

namespace OS
{
	Arch::Terminal *terminal;

	// ---------------------------------------

	void boot(Arch::Terminal *terminal, Arch::Cpu *cpu)
	{
		OS::terminal = terminal;
		terminal->println(Arch::Terminal::Type::Command, "Type commands here");
		terminal->println(Arch::Terminal::Type::App, "Apps output here");
		terminal->println(Arch::Terminal::Type::Kernel, "Kernel output here");
	}

	// ---------------------------------------

	void interrupt(const Arch::InterruptCode interrupt)
	{

		int typed = terminal->read_typed_char();
		if (terminal->is_alpha(typed))
		{
			terminal->print(Arch::Terminal::Type::Kernel, static_cast<char>(typed));
		}
		else if (terminal->is_num(typed))
		{
			terminal->print(Arch::Terminal::Type::Kernel, static_cast<char>(typed));
		}
		else if (terminal->is_backspace(typed))
		{
			terminal->print(Arch::Terminal::Type::Kernel, "\r");
		}
		else if (terminal->is_return(typed))
		{
			terminal->print(Arch::Terminal::Type::Kernel, "\n");
		}
	}

	// ---------------------------------------

	void syscall()
	{
	}

	// ---------------------------------------

} // end namespace OS
