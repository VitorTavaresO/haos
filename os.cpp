#include <stdexcept>
#include <string>
#include <string_view>
#include <array>
#include <list>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <time.h>

#include "config.h"
#include "lib.h"
#include "arq-sim.h"
#include "os.h"

namespace OS
{

	using Arch::PageTable;

	struct MemoryInterval
	{
		uint16_t start;
		uint16_t end;
	};

	struct Process
	{
		uint16_t pid;
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
		PageTable page_table;
		time_t start_application_time;
		time_t application_wakeup_time;
	};

	struct Frame
	{
		Process *process;
		bool free;
	};

	Arch::Terminal *terminal;
	Arch::Cpu *cpu;

	std::string typedCharacters;

	Process *current_process_ptr = nullptr;
	Process *idle_process_ptr = nullptr;

	std::list<Process *> ready_processes;
	std::list<Process *>::iterator ready_processes_begin = ready_processes.begin();
	std::list<Process *> blocked_processes;

	std::list<MemoryInterval> free_memory_intervals = {{0, Config::memsize_words - 1}};

	std::vector<Frame> free_frames(Config::memsize_words / Config::page_size_words, {nullptr, true});

	void panic(const std::string_view msg)
	{
		terminal->println(Arch::Terminal::Type::Kernel, "Kernel Panic: " + std::string(msg));
		cpu->turn_off();
	}

	void init_page_table(PageTable &page_table)
	{
		const uint32_t num_pages = Config::virtual_space_size / Config::page_size_words;
		page_table.frames.resize(num_pages);
		for (uint32_t i = 0; i < num_pages; ++i)
		{
			page_table.frames[i] = {i, false};
		}
	}

	uint32_t allocate_frame(Process *process)
	{
		for (uint32_t i = 0; i < free_frames.size(); ++i)
		{
			if (free_frames[i].free)
			{
				free_frames[i].free = false;
				free_frames[i].process = process;
				return i;
			}
		}
		return 0;
	}

	void desallocate_frame(Process *process)
	{
		for (auto &frame : free_frames)
		{
			if (frame.process == process)
			{
				frame.free = true;
				frame.process = nullptr;
			}
		}
	}

	std::list<MemoryInterval>::iterator find_free_memory_interval(const uint16_t size)
	{
		for (auto it = free_memory_intervals.begin(); it != free_memory_intervals.end(); ++it)
		{
			if (it->end - it->start + 1 >= size)
				return it;
		}
		return free_memory_intervals.end();
	}

	MemoryInterval allocate_memory(const uint16_t size)
	{
		auto iterator = find_free_memory_interval(size);
		if (iterator == free_memory_intervals.end())
			return {1, 0};

		MemoryInterval *interval = &(*iterator);

		MemoryInterval new_memory = {interval->start, uint16_t(interval->start + size - 1)};

		if (interval->end - interval->start + 1 == size)
			free_memory_intervals.erase(iterator);
		else
			interval->start += size;

		return new_memory;
	}

	void desallocate_memory(const MemoryInterval &memory)
	{
		for (uint32_t i = memory.start; i <= memory.end; ++i)
		{
			cpu->pmem_write(i, 0);
		}

		free_memory_intervals.push_back(memory);
	}

	Process *create_process(const std::string_view fname)
	{
		const uint32_t size = Lib::get_file_size_words(fname);
		if (size <= Config::memsize_words)
		{
			std::vector<uint16_t> bin = Lib::load_from_disk_to_16bit_buffer(fname);

			Process *process = new Process();

			MemoryInterval memory = allocate_memory(bin.size());

			if (memory.start == 1 && memory.end == 0)
			{
				terminal->println(Arch::Terminal::Type::Kernel, "Not enough memory to create process\n");
				return nullptr;
			}

			process->pc = 1;

			for (uint32_t i = 0; i < Config::nregs; i++)
				process->registers[i] = 0;

			process->state = Process::State::Ready;
			process->start_application_time = time(NULL);

			init_page_table(process->page_table);

			const uint32_t num_pages = (bin.size() / Config::page_size_words) + ((bin.size() % Config::page_size_words) != 0 ? 1 : 0);
			for (uint32_t i = 0; i < num_pages; ++i)
			{
				process->page_table.frames[i] = {allocate_frame(process), true};
			}

			for (uint32_t i = 0; i < bin.size(); i++)
			{
				uint32_t vaddr = i;
				uint32_t paddr = cpu->translate(&process->page_table, vaddr);
				cpu->pmem_write(paddr, bin[i]);
			}

			process->name = fname.substr(4);

			terminal->println(Arch::Terminal::Type::Kernel, "Process " + process->name + " created\n");

			if (process->name != "idle.bin")
			{
				ready_processes.push_back(process);
				ready_processes_begin = ready_processes.begin();
			}
			return process;
		}
		return nullptr;
	}

	void schedule_process(Process *process)
	{
		if (current_process_ptr != nullptr)
			panic("Process already scheduled");

		if (process->state != Process::State::Ready)
			panic("Process not ready");

		terminal->println(Arch::Terminal::Type::Kernel, "Running process: " + process->name + "\n");

		process->state = Process::State::Running;
		current_process_ptr = process;

		cpu->set_pc(process->pc);
		cpu->set_page_table(&process->page_table);

		for (uint32_t i = 0; i < Config::nregs; i++)
			cpu->set_gpr(i, process->registers[i]);
	}

	void unschedule_process()
	{
		Process *process = current_process_ptr;

		if (current_process_ptr == nullptr)
			panic("No process to unschedule");

		if (process->state != Process::State::Running)
			panic("Process not running");

		process->state = Process::State::Ready;
		for (uint32_t i = 0; i < Config::nregs; i++)
			process->registers[i] = cpu->get_gpr(i);

		process->pc = cpu->get_pc();

		current_process_ptr = nullptr;

		terminal->println(Arch::Terminal::Type::Kernel, "Unschedule process: " + process->name + "\n");
	}

	Process *search_process(const std::string_view fname)
	{
		for (auto it = ready_processes.begin(); it != ready_processes.end(); it++)
		{
			Process *process = *it;
			if (process->name == fname)
				return process;
		}
		for (auto it = blocked_processes.begin(); it != blocked_processes.end(); it++)
		{
			Process *process = *it;
			if (process->name == fname)
				return process;
		}
		return nullptr;
	}

	void round_robin()
	{
		if (current_process_ptr != idle_process_ptr && ready_processes.size() > 1)
		{
			if ((*ready_processes_begin)->state == Process::State::Ready)
			{
				Process *process = *ready_processes_begin;
				ready_processes.push_back(process);
				ready_processes.pop_front();
				ready_processes_begin = ready_processes.begin();

				unschedule_process();
				schedule_process(process);
			}
		}
	}
	void list_processes()
	{
		terminal->println(Arch::Terminal::Type::Command, "Processes:\n");
		for (auto it = ready_processes.begin(); it != ready_processes.end(); it++)
		{
			Process *process = *it;
			terminal->println(Arch::Terminal::Type::Command, process->name + "\n");
		}
	}

	void print_all_memory()
	{
		for (uint16_t i = 0; i < 60; i++)
		{
			terminal->print(Arch::Terminal::Type::Command, std::to_string(cpu->pmem_read(i)) + " ");
		}
		terminal->println(Arch::Terminal::Type::Command, "\n");
	}

	void sleep(Process *process, uint16_t time_to_sleep)
	{
		process->state = Process::State::Blocked;
		process->application_wakeup_time = time(NULL) + time_to_sleep;

		ready_processes.remove(process);

		blocked_processes.push_back(process);

		terminal->println(Arch::Terminal::Type::Kernel, "Process " + process->name + " going to sleep for " + std::to_string(time_to_sleep) + "\n");
	}

	void wakeup()
	{
		for (auto it = blocked_processes.begin(); it != blocked_processes.end();)
		{
			Process *process = *it;
			if (process->state == Process::State::Blocked && process->application_wakeup_time <= time(NULL))
			{
				process->state = Process::State::Ready;

				it = blocked_processes.erase(it);

				ready_processes.push_back(process);

				terminal->println(Arch::Terminal::Type::Kernel, "Process " + process->name + " woke up\n");
			}
			else
			{
				++it;
			}
		}
	}

	void kill(Process *process)
	{
		if (process->state == Process::State::Running)
		{
			panic("Process running");
		}

		desallocate_frame(process);
		terminal->println(Arch::Terminal::Type::Command, "Process " + process->name + " killed\n");
		terminal->println(Arch::Terminal::Type::Kernel, "Process " + process->name + " killed\n");

		// ready_processes.erase(std::remove(ready_processes.begin(), ready_processes.end(), process), ready_processes.end());
		// std::remove(ready_processes.begin(), ready_processes.end(), process);
		ready_processes.remove(process);
		delete process;

		ready_processes_begin = ready_processes.begin();
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

		else if (typedCharacters == "ls")
		{
			typedCharacters.clear();
			list_processes();
		}

		else if (typedCharacters == "mem")
		{
			typedCharacters.clear();
			print_all_memory();
		}

		else if (typedCharacters.find("kill ") == 0)
		{
			typedCharacters.erase(0, 5);
			std::string filename = typedCharacters;
			typedCharacters.clear();
			Process *process = search_process(filename);
			if (process != nullptr)
			{
				if (process == current_process_ptr)
				{
					Process *process_to_kill = current_process_ptr;
					if (ready_processes.size() == 1)
					{
						unschedule_process();
						schedule_process(idle_process_ptr);
						kill(process_to_kill);
					}
					else
					{
						round_robin();
						kill(process_to_kill);
					}
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
		if (idle_process_ptr == nullptr)
			panic("Idle process not created");
		else
			schedule_process(idle_process_ptr);
	}

	void interrupt(const Arch::InterruptCode interrupt)
	{
		wakeup();

		if (interrupt == Arch::InterruptCode::Keyboard)
			write_command();

		else if (interrupt == Arch::InterruptCode::Timer)
			round_robin();

		else if (interrupt == Arch::InterruptCode::GPF)
		{
			terminal->println(Arch::Terminal::Type::Kernel, "General Protection Fault\n");
			Process *process_to_kill = current_process_ptr;
			if (ready_processes.size() == 1)
			{
				unschedule_process();
				schedule_process(idle_process_ptr);
				kill(process_to_kill);
			}
			else
			{
				ready_processes_begin = ready_processes.begin();
				round_robin();
				kill(process_to_kill);
			}
		}
	}

	void syscall()
	{
		Process *process_to_kill = current_process_ptr;
		switch (cpu->get_gpr(0))
		{
		case 0:
			if (ready_processes.size() == 1)
			{
				unschedule_process();
				schedule_process(idle_process_ptr);
				kill(process_to_kill);
				break;
			}
			round_robin();
			kill(process_to_kill);
			break;

		case 1:
		{
			uint32_t v_addr = cpu->get_gpr(1);

			while (true)
			{
				uint32_t p_addr = cpu->translate(&current_process_ptr->page_table, v_addr);
				char ch = static_cast<char>(cpu->pmem_read(p_addr));

				if (ch == '\0')
					break;

				terminal->print(Arch::Terminal::Type::App, ch);
				v_addr++;
			}
			break;
		}
		case 2:
			terminal->println(Arch::Terminal::Type::App, "\n");
			break;
		case 3:
			terminal->println(Arch::Terminal::Type::App, cpu->get_gpr(1));
			break;

		case 6:
		{
			Process *process_to_sleep = current_process_ptr;
			time_t time_to_sleep = cpu->get_gpr(1);
			if (ready_processes.size() == 1)
			{
				unschedule_process();
				schedule_process(idle_process_ptr);
				sleep(process_to_sleep, time_to_sleep);
			}
			else
			{
				round_robin();
				sleep(process_to_sleep, time_to_sleep);
			}
			break;
		}
		case 7:
			time_t runtime = time(NULL) - current_process_ptr->start_application_time;
			terminal->println(Arch::Terminal::Type::Kernel, "Actual Application Time: " + std::to_string(runtime) + "\n");
			break;
		}
	}
}
