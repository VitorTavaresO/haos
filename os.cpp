#include <stdexcept>
#include <string>
#include <string_view>
#include <array>

#include <cstdint>
#include <cstdlib>

#include "config.h"
#include "lib.h"
#include "arq-sim.h"
#include "os.h"

namespace OS
{
	Arch::Terminal *terminal;
	Arch::Cpu *cpu;

	std::string typedCharacters;

	Process current_process;

	// ---------------------------------------

	struct Process
	{
		uint16_t pc;
		std::array<uint16_t, Config::nregs> registers;
		enum class State
		{
			Running,
			Ready,
			Blocked
		};
		State state;
	};

	// ---------------------------------------

	Process *create_process(std::string_view fname)
	{
		if (Lib::get_file_size_words(fname) <= Config::memsize_words)
		{
			std::vector<uint16_t> bin = Lib::load_from_disk_to_16bit_buffer(fname);

			Process *process = new Process();
			process->pc = 1;

			for (int i = 0; i < Config::nregs; i++)
			{
				process->registers[i] = 0;
			}

			process->state = Process::State::Ready;

			for (int i = 0; i < bin.size(); i++)
			{
				cpu->pmem_write(i, bin[i]);
			}

			return process;
		}
	}

	void verify_command()
	{
		if (typedCharacters == "quit")
		{
			typedCharacters.clear();
			exit(0);
		}

		else
		{
			terminal->println(Arch::Terminal::Type::Command, "Unknown command");
			typedCharacters.clear();
		}
	}

	void write_command()
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
			terminal->print(Arch::Terminal::Type::Command, "\n");
			verify_command();
		}
	}

	void boot(Arch::Terminal *terminal, Arch::Cpu *cpu)
	{
		OS::terminal = terminal;
		terminal->println(Arch::Terminal::Type::Command, "Type commands here");
		terminal->println(Arch::Terminal::Type::App, "Apps output here");
		terminal->println(Arch::Terminal::Type::Kernel, "Kernel output here");
		current_process = create_process("print.bin");
	}

	// ---------------------------------------

	void interrupt(const Arch::InterruptCode interrupt)
	{
		if (interrupt == Arch::InterruptCode::Keyboard)
		{
			write_command();
		}
	}

	// ---------------------------------------

	void syscall()
	{
	}

	// ---------------------------------------

} // end namespace OS
