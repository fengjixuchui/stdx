
#include <stdx/async/task.h>
#include <iostream>
#include <stdx/file.h>
//#include <stdx/net/socket.h>
#include <sstream>
#include <stdx/string.h>
#include <sstream>
int main(int argc, char **argv)
{
//#define ENABLE_WEB
#ifdef ENABLE_WEB
#pragma region web_test
	stdx::network_io_service service;
	stdx::socket s = stdx::open_socket(service, stdx::addr_family::ip, stdx::socket_type::stream, stdx::protocol::tcp);
	try
	{
		stdx::network_addr addr("127.0.0.1", 8080);
		s.bind(addr);
	}
	catch (std::exception &e)
	{
		std::cerr << e.what();
		return -1;
	}
	std::cout << "�Ѽ���http://localhost:8080" << std::endl;
	s.listen(65535);
	stdx::file_io_service file_io_service;
	while (true)
	{
		auto c = s.accept();
		/*c.recv_utill_error(1024, [c,file_io_service](stdx::network_recv_event e) mutable
		{
			stdx::file_stream stream = stdx::open_file(file_io_service, "./index.html", stdx::file_access_type::read, stdx::file_open_type::open);
			stream.read_to_end(0).then([c](stdx::file_read_event e) mutable
			{
				std::string str = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8;\r\nContent-Length:";
				str.append(std::to_string(e.buffer.size()));
				str.append("\r\n\r\n");
				str.append(e.buffer);
				c.send(str.c_str(), str.size()).then([](stdx::task_result<stdx::network_send_event> &e)
				{
					try
					{
						e.get();
					}
					catch (const std::exception&err)
					{
						std::cerr << err.what() <<std::endl;
					}
				});
			});
		}, [c](std::exception_ptr &err)
		{
			if (err)
			{
				try
				{
					std::rethrow_exception(err);
				}
				catch (const std::system_error &e)
				{
					std::cerr <<e.code().value() <<std::endl<< e.what()<<std::endl;
				}
			}
		});*/
		c.recv(1024).then([file_io_service, c]()
		{
			stdx::file_stream stream = stdx::open_file(file_io_service, "./index.html", stdx::file_access_type::read, stdx::file_open_type::open);
			stream.read_to_end(0).then([c](stdx::file_read_event e) mutable
			{
				std::string str = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8;\r\nContent-Length:";
				str.append(std::to_string(e.buffer.size()));
				str.append("\r\n\r\n");
				str.append(e.buffer);
				c.send(str.c_str(), str.size()).then([c](stdx::task_result<stdx::network_send_event> &e) mutable
				{
					c.close();
				});
			});
		});
	}
#pragma endregion
#endif 
//#define ENABLE_FILE_TO_HEADER
#ifdef ENABLE_FILE_TO_HEADER
	size_t size = argc - 1;
	std::cout << "���������ļ�ת��	��" << size << "��" << "......" << std::endl;
	std::cout << "�����ļ�IO����......";
	stdx::file_io_service io_service;
	std::cout << "done!" << std::endl;
	std::cout << "��ʼ�ļ�ת��......" << std::endl;
	int errs = 0;
	int *errs_ptr = &errs;
	std::shared_ptr<std::atomic_int> nr_done_ptr = std::make_shared<std::atomic_int>(0);

	for (size_t i = 1; i <= size; i++)
	{
		std::cout << "����ת��	��" << i << "��	��" << size << "��......" << std::endl;
		try
		{
			std::string path = argv[i];
			stdx::replace_string<std::string>(path, "\\", "/");
			auto file = stdx::open_file(io_service,path, stdx::file_access_type::read, stdx::file_open_type::open);
			file.read_to_end(0).then([errs_ptr, i, io_service, argv,&path](stdx::task_result<stdx::file_read_event> &r) mutable
			{
				try
				{
					auto e = r.get();
					std::string new_path = argv[i];
					new_path.append(".h");
					auto new_file = stdx::open_file(io_service, new_path, stdx::file_access_type::write, stdx::file_open_type::create);
					std::stringstream builder;
					std::vector<std::string> tmp;
					std::string chars = "/";
					stdx::spit_string(new_path, chars, tmp);
					stdx::replace_string<std::string>(tmp.back(), ".", "_");
					builder << "#pragma once" << next_line
						<< "namespace __files" << next_line
						<< "{" << next_line
						<< "//" << argv[i] << next_line
						<< "    static char " << tmp.back() << "[] " << "= {";
					for (size_t i = 0; i < e.buffer.size(); i++)
					{
						builder << (int)e.buffer[i];
						if (i != (e.buffer.size() - 1))
						{
							builder << ",";
						}
					}
					builder << "};" << next_line
						<< "}" << next_line;
					std::string content = builder.str();
					new_file.write(content, 0).wait();
				}
				catch (const std::system_error &e)
				{
					*errs_ptr += 1;
					std::cerr << "ת����������:" << e.code().message() << std::endl << "�������:" << e.code().value() << std::endl;
					std::cout << "������" << i << "��......" << std::endl;
				}
			}).then([&i, &size, nr_done_ptr]()
			{
				*nr_done_ptr += 1;
				std::cout << "ת�����	��" << i << "��	��" << size << "��......" << std::endl;
			}).wait();
		}
		catch (const std::system_error &e)
		{
			errs += 1;
			std::cerr << "ת����������:" << e.code().message() << std::endl << "�������:" << e.code().value() << std::endl;
			std::cout << "������" << i << "��......" << std::endl;
		}
	}
	std::cout << "done!" << std::endl;
	int success = size - errs;
	std::cout << "ת�������:	" << "Success(s):" << success << "	Error(s):" << errs << std::endl;
#endif // ENABLE_FILE_TO_HEADER
#define ENABLE_FILE
#ifdef ENABLE_FILE
	stdx::file_io_service service;
	int fd = service.create_file("./a.txt", stdx::file_access_type::all, stdx::file_open_type::open);
	std::string str = "Hello World!!!!!!!!!";
	service.write_file(fd, str.c_str(), str.size(), 0, [](stdx::file_write_event e, std::exception_ptr error)
	{
		try
		{
			if (error)
			{
				std::rethrow_exception(error);
			}
			std::cout << "success in writing file! bytes:" << e.size << std::endl;
		}
		catch (const std::exception&err)
		{
			std::cerr << err.what();
		}
	});
	service.read_file(fd, str.size(), 0, [](stdx::file_read_event e, std::exception_ptr error)
	{
		try
		{
			if (error)
			{
				std::rethrow_exception(error);
			}
			std::cout << "success in reading file! bytes:" << e.buffer << std::endl;
		}
		catch (const std::exception&err)
		{
			std::cerr << err.what();
		}
	});
	close(fd);
	std::cin.get();
#endif // ENABLE_FILE
	return 0;
}