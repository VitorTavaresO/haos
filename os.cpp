#include <stdexcept>
#include <string>
#include <string_view>
#include <array>
#include <list>

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

	struct Process
	{
		std::string name;
		uint16_t pc;
		std::array<uint16_t, Config::nregs> registers;
		enum class State
		{
			Running,
			Ready,
			Blocked
		};
		State state;
		uint16_t baser;
		uint16_t limitr;
	};

	Process *current_process_ptr = nullptr;
	Process *idle_process_ptr = nullptr;

	std::list<Process *> ready_processes;
	std::list<Process *>::iterator ready_processes_begin = ready_processes.begin();

	void panic(const std::string_view msg)
	{
		terminal->println(Arch::Terminal::Type::Kernel, "Kernel Panic: " + std::string(msg));
		cpu->turn_off();
	}

	Process *create_process(const std::string_view fname)
	{
		const uint32_t size = Lib::get_file_size_words(fname);
		if (size <= Config::memsize_words)
		{
			std::vector<uint16_t> bin = Lib::load_from_disk_to_16bit_buffer(fname);

			Process *process = new Process();
			process->pc = 1;

			if (idle_process_ptr == nullptr)
				process->baser = 0;

			else if (ready_processes.empty())
				process->baser = idle_process_ptr->limitr + 1;

			else
			{

				Process *last_process = ready_processes.back();
				process->baser = last_process->limitr + 1;
			}

			process->limitr = (process->baser + size) - 1;

			for (uint32_t i = 0; i < Config::nregs; i++)
				process->registers[i] = 0;

			process->state = Process::State::Ready;

			for (uint32_t i = 0; i < bin.size(); i++)
				cpu->pmem_write(i + process->baser, bin[i]);

			if (process != idle_process_ptr)
			{
				ready_processes.push_back(process);
				ready_processes_begin = ready_processes.begin();
			}

			process->name = fname.substr(4);

			terminal->println(Arch::Terminal::Type::Kernel, "Process " + process->name + " created\n");

			return process;
		}
		return nullptr;
	}

	void unschedule_process()
	{
		Process *process = current_process_ptr;

		if (current_process_ptr == nullptr)
			panic("No process to unschedule");

		process->state = Process::State::Ready;
		for (uint32_t i = 0; i < Config::nregs; i++)
			process->registers[i] = cpu->get_gpr(i);

		process->pc = cpu->get_pc();

		current_process_ptr = nullptr;

		terminal->println(Arch::Terminal::Type::Kernel, "Unschedule process: " + process->name + "\n");
	}

	void schedule_process(Process *process)
	{
		if (current_process_ptr != nullptr)
			panic("Process already scheduled");

		terminal->println(Arch::Terminal::Type::Kernel, "Running process: " + process->name + "\n");

		process->state = Process::State::Running;
		current_process_ptr = process;

		cpu->set_pc(process->pc);
		cpu->set_vmem_paddr_init(process->baser);
		cpu->set_vmem_paddr_end(process->limitr);

		for (uint32_t i = 0; i < Config::nregs; i++)
			cpu->set_gpr(i, process->registers[i]);
	}

	Process *search_process(const std::string_view fname)
	{
		for (auto it = ready_processes.begin(); it != ready_processes.end(); it++)
		{
			Process *process = *it;
			if (process->name == fname)
				return process;
		}
		return nullptr;
	}

	void round_robin()
	{
		if (current_process_ptr == idle_process_ptr)
			return;

		if (ready_processes.empty())
			return;

		ready_processes_begin++;
		if (ready_processes_begin == ready_processes.end())
		{
			ready_processes_begin = ready_processes.begin();
		}

		Process *process = *ready_processes_begin;

		unschedule_process();
		schedule_process(process);
	}

	void kill(Process *process)
	{
		for (uint32_t i = process->baser; i <= process->limitr; i++)
			cpu->pmem_write(i, 0);

		terminal->println(Arch::Terminal::Type::Command, "Process " + process->name + " killed\n");
		terminal->println(Arch::Terminal::Type::Kernel, "Process " + process->name + " killed\n");
		ready_processes.erase(std::remove(ready_processes.begin(), ready_processes.end(), process), ready_processes.end());
		delete process;
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
			typedCharacters.erase(0, 4);
			std::string filename = typedCharacters;
			typedCharacters.clear();
			if (std::filesystem::exists(filename))
			{
				terminal->println(Arch::Terminal::Type::Command, "Running file:" + filename + "\n");
				unschedule_process();
				schedule_process(create_process(filename));
			}
			else
			{
				terminal->println(Arch::Terminal::Type::Command, "File " + filename + " not found\n");
			}
		}
		else if (typedCharacters.find("kill ") == 0)
		{
			typedCharacters.erase(0, 5);
			std::string filename = typedCharacters;
			typedCharacters.clear();
			if (current_process_ptr != idle_process_ptr)
			{
				Process *process = search_process(filename);
				if (process != nullptr)
				{
					if (process == current_process_ptr)
					{
						unschedule_process();
						kill(process);
						schedule_process(idle_process_ptr);
					}
					else
					{
						kill(process);
					}
				}
				else
				{
					terminal->println(Arch::Terminal::Type::Command, "No process with this name to kill\n");
				}
			}
			else
			{
				terminal->println(Arch::Terminal::Type::Command, "No process to kill");
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
		idle_process_ptr = create_process("bin/idle.bin");
		schedule_process(idle_process_ptr);
	}

	void interrupt(const Arch::InterruptCode interrupt)
	{
		if (interrupt == Arch::InterruptCode::Keyboard)
			write_command();

		if (interrupt == Arch::InterruptCode::Timer)
		{
			round_robin();
		}
		else if (interrupt == Arch::InterruptCode::GPF)
		{
			terminal->println(Arch::Terminal::Type::Kernel, "General Protection Fault\n");
			if (current_process_ptr != idle_process_ptr)
			{
				Process *process_to_kill = current_process_ptr;
				unschedule_process();
				kill(process_to_kill);
				schedule_process(idle_process_ptr);
			}
		}
	}

	void syscall()
	{

		Process *process_to_kill = current_process_ptr;
		switch (cpu->get_gpr(0))
		{
		case 0:
			round_robin();
			kill(process_to_kill);
			break;
		case 1:
		{
			uint16_t addr = cpu->get_gpr(1);

			addr = addr + current_process_ptr->baser;

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
}
