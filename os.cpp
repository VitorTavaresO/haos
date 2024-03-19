#include <stdexcept>
#include <string>
#include <string_view>
#include <array>

#include <cstdint>
#include <cstdlib>
#include <filesystem>

#include "config.h"
#include "lib.h"
#include "arq-sim.h"
#include "os.h"

namespace OS
{
	Arch::Terminal *terminal;
	Arch::Cpu *cpu;

	std::string typedCharacters;

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

	Process *current_process_ptr = nullptr;

	// ---------------------------------------

	Process *create_process(std::string_view fname)
	{
		if (Lib::get_file_size_words(fname) <= Config::memsize_words)
		{
			std::vector<uint16_t> bin = Lib::load_from_disk_to_16bit_buffer(fname);

			Process *process = new Process();
			process->pc = 1;

			for (uint32_t i = 0; i < Config::nregs; i++)
			{
				process->registers[i] = 0;
			}

			process->state = Process::State::Ready;

			for (uint32_t i = 0; i < bin.size(); i++)
			{
				cpu->pmem_write(i, bin[i]);
			}

			return process;
		}
		return nullptr;
	}

	void schedule_process(Process *current_process_ptr)
	{
		cpu->set_pc(current_process_ptr->pc);
		for (uint32_t i = 0; i < Config::nregs; i++)
		{
			cpu->set_gpr(i, current_process_ptr->registers[i]);
		}
	}

	void verify_command()
	{
		if (typedCharacters == "quit")
		{
			typedCharacters.clear();
			cpu->turn_off();
		}

		else if (typedCharacters.find("run ") == 0)
		{
			typedCharacters = typedCharacters.substr(4);

			if (typedCharacters.find("-file ") == 0)
			{
				std::string_view filename = typedCharacters.substr(6);
				typedCharacters.clear();
				if (std::filesystem::exists(filename))
				{
					terminal->println(Arch::Terminal::Type::Command, "Running file:" + std::string(filename) + "\n");
					current_process_ptr = create_process(filename);
					schedule_process(current_process_ptr);
				}
				else
				{
					terminal->println(Arch::Terminal::Type::Command, "File not found\n");
				}
			}
			else
			{
				terminal->println(Arch::Terminal::Type::Command, "Unknown command");
				typedCharacters.clear();
			}
		}
		else if (typedCharacters == "kill")
		{
			typedCharacters.clear();
			if (current_process_ptr != nullptr)
			{
				delete current_process_ptr;
				current_process_ptr = nullptr;
				terminal->println(Arch::Terminal::Type::Command, "Current process killed\n");
			}
			else
			{
				terminal->println(Arch::Terminal::Type::Command, "No process to kill\n");
			}
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

		if (terminal->is_alpha(typed) || terminal->is_num(typed) || typed == ' ' || typed == '-' || typed == '.' || typed == '/')
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
		OS::cpu = cpu;
		terminal->println(Arch::Terminal::Type::Command, "Type commands here");
		terminal->println(Arch::Terminal::Type::App, "Apps output here");
		terminal->println(Arch::Terminal::Type::Kernel, "Kernel output here");
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
		switch (cpu->get_gpr(0))
		{
		case 0:
			cpu->turn_off();
			break;
		case 1:
		{
			uint16_t addr = cpu->get_gpr(1);
			while (cpu->pmem_read(addr) != 0)
			{

				terminal->print(Arch::Terminal::Type::App, static_cast<char>(cpu->pmem_read(addr)));
				addr++;
			}
			break;
		}
		case 2:
			terminal->println(Arch::Terminal::Type::App, "\n");
			break;
		case 3:
			terminal->println(Arch::Terminal::Type::App, cpu->get_gpr(1));
			break;
		}
	}

	// ---------------------------------------

} // end namespace OS
