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

	std::string typedCharacters;

	void keyboard()
	{
		int typed = terminal->read_typed_char();

		if (terminal->is_alpha(typed) || terminal->is_num(typed) || typed == ' ')
		{
			typedCharacters.push_back(static_cast<char>(typed));
			terminal->print(Arch::Terminal::Type::Command, static_cast<char>(typed));
		}

		else if (terminal->is_backspace(typed))
		{
			if (!typedCharacters.empty())
			{
				typedCharacters.pop_back();
				terminal->print(Arch::Terminal::Type::Command, "\r");
				terminal->print(Arch::Terminal::Type::Command, typedCharacters);
			}
		}

		else if (terminal->is_return(typed))
		{
			typedCharacters.push_back('\n');
			terminal->print(Arch::Terminal::Type::Command, "\n");
		}
	}

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
		if (interrupt == Arch::InterruptCode::Keyboard)
		{
			keyboard();
		}
	}

	// ---------------------------------------

	void syscall()
	{
	}

	// ---------------------------------------

} // end namespace OS
