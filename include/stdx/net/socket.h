﻿#pragma once
#include <stdx/async/task.h>
#include <stdx/async/spin_lock.h>
#include <stdx/io.h>
#include <stdx/env.h>
#ifdef WIN32
#include <WinSock2.h>
#include <MSWSock.h>
#pragma comment(lib,"Ws2_32.lib ")
#endif 
namespace stdx
{
#ifdef WIN32
#define _ThrowWSAError 	auto _ERROR_CODE = WSAGetLastError(); \
						if(_ERROR_CODE != WSA_IO_PENDING)\
						{\
							std::string _ERROR_STR("windows WSA error:");\
							_ERROR_STR.append(std::to_string(_ERROR_CODE));\
							throw std::system_error(std::error_code(_ERROR_CODE,std::system_category()),_ERROR_STR.c_str());\
						}\

	struct _WSAStarter
	{
		WSAData wsa;
		_WSAStarter()
			:wsa()
		{
			if (WSAStartup(MAKEWORD(2, 2), &wsa))
			{
				_ThrowWinError
			}
		}
		~_WSAStarter()
		{
			if (WSACleanup())
			{
				_ThrowWinError
			}
		}
	};
	_WSAStarter _wsastarter;
	struct protocol
	{
		enum
		{
			ip = IPPROTO_IP,
			tcp = IPPROTO_TCP,
			udp = IPPROTO_UDP
		};
	};
	struct socket_type
	{
		enum
		{
			raw = SOCK_RAW,
			stream = SOCK_STREAM,
			dgram = SOCK_DGRAM
		};
	};
	struct addr_family
	{
		enum
		{
			ip = AF_INET,
			ipv6 = AF_INET6
		};
	};

	class network_addr
	{
	public:
		network_addr()=default;
		network_addr(unsigned long ip,const uint16 &port)
		{
			m_handle.sin_family = addr_family::ip;
			m_handle.sin_addr.S_un.S_addr = ip;
			m_handle.sin_port = htons(port);
		}
		network_addr(const char *ip, const uint16 &port)
			:network_addr(inet_addr(ip),port)
		{}
		network_addr(const network_addr &other)
		{
			m_handle = other.m_handle;
		}
		~network_addr() = default;
		operator SOCKADDR_IN* ()
		{
			return &m_handle;
		}

		operator sockaddr*()
		{
			return (sockaddr*)&m_handle;
		}

		network_addr &operator=(const network_addr &other)
		{
			m_handle = other.m_handle;
		}
		const static int addr_len = sizeof(sockaddr);
	private:
		SOCKADDR_IN m_handle;
	};
	struct network_io_context
	{
		network_io_context()
		{
			std::memset(&m_ol, 0, sizeof(OVERLAPPED));
		}
		~network_io_context() = default;
		WSAOVERLAPPED m_ol;
		SOCKET this_socket;
		network_addr addr;
		WSABUF buffer;
		DWORD size;
		SOCKET target_socket;
		std::function <void(network_io_context*,std::exception_ptr)> *callback;
	};

	struct network_send_event
	{
		network_send_event() = default;
		~network_send_event() = default;
		network_send_event(const network_send_event &other)
			:sock(other.sock)
			, size(other.size)
		{}
		network_send_event(network_send_event &&other)
			:sock(std::move(other.sock))
			, size(std::move(other.size))
		{}
		network_send_event &operator=(const network_send_event &other)
		{
			sock = other.sock;
			size = other.size;
			return *this;
		}
		network_send_event(network_io_context *ptr)
			:sock(ptr->this_socket)
			, size(ptr->size)
		{}
		SOCKET sock;
		size_t size;
	};

	struct network_recv_event 
	{
		network_recv_event() = default;
		~network_recv_event() = default;
		network_recv_event(const network_recv_event &other)
			:sock(other.sock)
			,buffer(other.buffer)
			,size(other.size)
		{}
		network_recv_event(network_recv_event &&other)
			:sock(std::move(other.sock))
			,buffer(other.buffer)
			,size(other.size)
		{}
		network_recv_event &operator=(const network_recv_event &other)
		{
			sock = other.sock;
			buffer = other.buffer;
			size = other.size;
			return *this;
		}
		network_recv_event(network_io_context *ptr)
			:sock(ptr->target_socket)
			,buffer(ptr->buffer.len, ptr->buffer.buf)
			,size(ptr->size)
		{}
		SOCKET sock;
		stdx::buffer buffer;
		size_t size;
	};

	class _NetworkIOService
	{
	public:
		using iocp_t = stdx::iocp<network_io_context>;
		_NetworkIOService()
			:m_iocp()
		{}
		_NetworkIOService(const iocp_t &iocp)
			:m_iocp(iocp)
		{}
		delete_copy(_NetworkIOService);
		~_NetworkIOService() = default;
		SOCKET create_socket(const int &addr_family, const int &sock_type, const int &protocl)
		{
			SOCKET sock = socket(addr_family,sock_type,protocl);
			if (sock == INVALID_SOCKET)
			{
				_ThrowWSAError
			}
			m_iocp.bind(sock);
			return sock;
		}
		SOCKET create_wsasocket(const int &addr_family,const int &sock_type,const int &protocl)
		{
			SOCKET sock = WSASocket(addr_family, sock_type,protocl, NULL, 0,WSA_FLAG_OVERLAPPED);
			if (sock == INVALID_SOCKET)
			{
				_ThrowWSAError
			}
			m_iocp.bind(sock);
			return sock;
		}
		//发送数据
		void send(SOCKET sock,const char* data,const size_t &size,std::function<void(network_send_event,std::exception_ptr)> &&callback)
		{
			auto *context_ptr = new network_io_context;
			context_ptr->this_socket = sock;
			char *buffer = (char*)std::calloc(sizeof(char), size);
			std::strncpy(buffer, data, size);
			context_ptr->buffer.buf = buffer;
			context_ptr->buffer.len = size;
			auto *call = new std::function <void(network_io_context*, std::exception_ptr)>;
			*call = [callback](network_io_context *context_ptr, std::exception_ptr error)
			{
				if (error)
				{
					std::free(context_ptr->buffer.buf);
					delete context_ptr;
					callback(network_send_event(), error);
					return;
				}
				network_send_event context(context_ptr);
				std::free(context_ptr->buffer.buf);
				delete context_ptr;
				callback(context, nullptr);
			};
			context_ptr->callback = call;
			if (WSASend(sock, &(context_ptr->buffer), 1, &(context_ptr->size), NULL, &(context_ptr->m_ol), NULL) == SOCKET_ERROR)
			{
				_ThrowWSAError
			}
			stdx::threadpool::run([](iocp_t iocp) 
			{
				auto *context_ptr = iocp.get();
				std::exception_ptr error(nullptr);
				try
				{
					DWORD flag = 0;
					if (!WSAGetOverlappedResult(context_ptr->this_socket, &(context_ptr->m_ol),&(context_ptr->size), false, &flag))
					{
						//在这里出错
						_ThrowWSAError
					}
				}
				catch (const std::exception&)
				{
					error = std::current_exception();
				}
				auto *call = context_ptr->callback;
				try
				{
					(*call)(context_ptr, error);
				}
				catch (const std::exception&)
				{
				}
				delete call;
			},m_iocp);
		}

		//接收数据
		void recv(SOCKET sock,const size_t &size,std::function<void(network_recv_event,std::exception_ptr)> &&callback)
		{
			auto *context_ptr = new network_io_context;
			context_ptr->this_socket = sock;
			char *buf = (char*)std::calloc(sizeof(char), size);
			context_ptr->buffer.buf = buf;
			context_ptr->buffer.len = size;
			auto *call = new std::function <void(network_io_context*,std::exception_ptr)>;
			*call = [callback](network_io_context *context_ptr,std::exception_ptr error)
			{
				if (error)
				{
					std::free(context_ptr->buffer.buf);
					delete context_ptr;
					callback(network_recv_event(),error);
					return;
				}
				network_recv_event context(context_ptr);
				delete context_ptr;
				callback(context,std::exception_ptr(nullptr));
			};
			context_ptr->callback = call;

			if (WSARecv(sock, &(context_ptr->buffer), 1, &(context_ptr->size),&(_NetworkIOService::recv_flag), &(context_ptr->m_ol), NULL) == SOCKET_ERROR)
			{
				_ThrowWSAError
			}
			stdx::threadpool::run([](iocp_t iocp)
			{
				auto *context_ptr = iocp.get();
				std::exception_ptr error(nullptr);
				try
				{
					DWORD flag = 0;
					if (!WSAGetOverlappedResult(context_ptr->this_socket, &(context_ptr->m_ol), &(context_ptr->size), false,&flag))
					{
						//在这里出错
						_ThrowWSAError
					}
				}
				catch (const std::exception&)
				{
					error = std::current_exception();
				}
				auto *call = context_ptr->callback;
				try
				{
					(*call)(context_ptr, error);
				}
				catch (const std::exception&)
				{
				}
				delete call;
			}, m_iocp);
		}

		void connect(SOCKET sock,stdx::network_addr &addr) 
		{
			if (WSAConnect(sock, addr, network_addr::addr_len, NULL, NULL, NULL, NULL) == SOCKET_ERROR)
			{
				_ThrowWSAError
			}
		}

		SOCKET accept(SOCKET sock,network_addr &addr)
		{
			int size = network_addr::addr_len;
			SOCKET s = WSAAccept(sock,addr,&size, NULL, NULL);
			if (s == INVALID_SOCKET)
			{
				_ThrowWSAError
			}
			m_iocp.bind(s);
			return s;
		}

		void listen(SOCKET sock,int backlog)
		{
			if (::listen(sock, backlog) == SOCKET_ERROR)
			{
				_ThrowWSAError
			}
		}

		void bind(SOCKET sock,network_addr &addr)
		{
			if (::bind(sock, addr,network_addr::addr_len)==SOCKET_ERROR)
			{
				_ThrowWSAError
			}
		}

		void send_to(SOCKET sock,const network_addr &addr,const char *data,const size_t &size,std::function<void(stdx::network_send_event,std::exception_ptr)> &&callback)
		{
			stdx::network_io_context *context_ptr = new stdx::network_io_context;
			context_ptr->addr = addr;
			context_ptr->this_socket = sock;
			char *buf = (char*)std::calloc(sizeof(char),size);
			context_ptr->buffer.buf = buf;
			context_ptr->buffer.len = size;
			auto *call = new std::function <void(network_io_context*, std::exception_ptr)>;
			*call = [callback](network_io_context *context_ptr, std::exception_ptr error)
			{
				if (error)
				{
					std::free(context_ptr->buffer.buf);
					delete context_ptr;
					callback(network_send_event(), error);
					return;
				}
				network_send_event context(context_ptr);
				std::free(context_ptr->buffer.buf);
				delete context_ptr;
				callback(context, nullptr);
			};
			context_ptr->callback = call;
			if (WSASendTo(sock, &(context_ptr->buffer), 1, &(context_ptr->size),NULL, (context_ptr->addr),network_addr::addr_len, &(context_ptr->m_ol), NULL) == SOCKET_ERROR)
			{
				_ThrowWSAError
			}
			stdx::threadpool::run([](iocp_t iocp)
			{
				auto *context_ptr = iocp.get();
				std::exception_ptr error(nullptr);
				try
				{
					DWORD flag = 0;
					if (!WSAGetOverlappedResult(context_ptr->this_socket, &(context_ptr->m_ol), &(context_ptr->size), false, &flag))
					{
						//在这里出错
						_ThrowWSAError
					}
				}
				catch (const std::exception&)
				{
					error = std::current_exception();
				}
				auto *call = context_ptr->callback;
				try
				{
					(*call)(context_ptr, error);
				}
				catch (const std::exception&)
				{
				}
				delete call;
			}, m_iocp);
		}

		void recv_from(SOCKET sock,const network_addr &addr, const size_t &size, std::function<void(network_recv_event, std::exception_ptr)> &&callback)
		{
			auto *context_ptr = new network_io_context;
			context_ptr->this_socket = sock;
			char *buf = (char*)std::calloc(sizeof(char), size);
			context_ptr->buffer.buf = buf;
			context_ptr->buffer.len = size;
			auto *call = new std::function <void(network_io_context*, std::exception_ptr)>;
			*call = [callback](network_io_context *context_ptr, std::exception_ptr error)
			{
				if (error)
				{
					std::free(context_ptr->buffer.buf);
					delete context_ptr;
					callback(network_recv_event(), error);
					return;
				}
				network_recv_event context(context_ptr);
				delete context_ptr;
				callback(context, std::exception_ptr(nullptr));
			};
			context_ptr->callback = call;
			if (WSARecvFrom(sock, &(context_ptr->buffer), 1, &(context_ptr->size),&(_NetworkIOService::recv_flag),context_ptr->addr,(LPINT)&(network_addr::addr_len), &(context_ptr->m_ol), NULL) == SOCKET_ERROR)
			{
				_ThrowWSAError
			}
			stdx::threadpool::run([](iocp_t iocp)
			{
				auto *context_ptr = iocp.get();
				std::exception_ptr error(nullptr);
				try
				{
					DWORD flag = 0;
					if (!WSAGetOverlappedResult(context_ptr->this_socket, &(context_ptr->m_ol), &(context_ptr->size), false, &flag))
					{
						//在这里出错
						_ThrowWSAError
					}
				}
				catch (const std::exception&)
				{
					error = std::current_exception();
				}
				auto *call = context_ptr->callback;
				try
				{
					(*call)(context_ptr, error);
				}
				catch (const std::exception&)
				{
				}
				delete call;
			}, m_iocp);
		}
	private:
		iocp_t m_iocp;
		static DWORD recv_flag;
	};
	DWORD _NetworkIOService::recv_flag = 0;

	class network_io_service
	{
		using iocp_t = _NetworkIOService::iocp_t;
		using impl_t = std::shared_ptr<_NetworkIOService>;
	public:
		network_io_service()
			:m_impl(std::make_shared<_NetworkIOService>())
		{}
		network_io_service(const iocp_t &iocp)
			:m_impl(std::make_shared<_NetworkIOService>(iocp))
		{}
		network_io_service(const network_io_service &other)
			:m_impl(other.m_impl)
		{}
		network_io_service(network_io_service &&other)
			:m_impl(std::move(other.m_impl))
		{}
		network_io_service &operator=(const network_io_service &other)
		{
			m_impl = other.m_impl;
			return *this;
		}
		~network_io_service() = default;
		SOCKET create_socket(const int &addr_family,const int &sock_type,const int &protocl)
		{
			return m_impl->create_wsasocket(addr_family, sock_type, protocl);
		}
		void send(SOCKET sock, const char* data, const size_t &size, std::function<void(network_send_event, std::exception_ptr)> &&callback)
		{
			m_impl->send(sock, data, size, std::move(callback));
		}
		void recv(SOCKET sock, const size_t &size, std::function<void(network_recv_event, std::exception_ptr)> &&callback)
		{
			m_impl->recv(sock, size, std::move(callback));
		}
		void connect(SOCKET sock, stdx::network_addr &addr)
		{
			m_impl->connect(sock, addr);
		}
		SOCKET accept(SOCKET sock, network_addr &addr)
		{
			return m_impl->accept(sock, addr);
		}
		void listen(SOCKET sock, int backlog)
		{
			m_impl->listen(sock, backlog);
		}
		void bind(SOCKET sock, network_addr &addr)
		{
			m_impl->bind(sock, addr);
		}
		void send_to(SOCKET sock, const network_addr &addr, const char *data, const size_t &size, std::function<void(stdx::network_send_event, std::exception_ptr)> &&callback)
		{
			m_impl->send_to(sock, addr, data, size, std::move(callback));
		}
		void recv_from(SOCKET sock, const network_addr &addr, const size_t &size, std::function<void(network_recv_event, std::exception_ptr)> &&callback)
		{
			m_impl->recv_from(sock, addr, size, std::move(callback));
		}
	private:
		impl_t m_impl;
	};
	//class _Socket
	//{
	//public:
	//	_Socket(int addr_family,int sock_type,int protocl)
	//		:_Socket(WSASocket(addr_family, sock_type, protocl, NULL, 0, WSAOVERLAPPED))
	//	{}
	//	~_Socket()
	//	{
	//		closesocket(handle);
	//	}
	//	SOCKET accept_async(DWORD buffer_size)
	//	{
	//		if (accept_ex==NULL)
	//		{
	//			_GetAcceptEx(m_handle, &accept_ex);
	//		}
	//		SOCKET new_socket = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	//		char *buffer = std::calloc(sizeof(char), buffer_size);
	//		DWORD size;
	//		
	//		accept_ex(m_handle, new_socket, buf->data(), buf->size() - ((sizeof(sockaddr_in) + 16) * 2), sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &size, &ol);
	//		return new_socket;
	//	}
	//	void use_io_service(const stdx::socket_io_service &io_service)
	//	{
	//		io_service.bind(m_handle);
	//	}
	//private:
	//	SOCKET m_handle;
	//	_Socket(SOCKET handle)
	//		:m_handle(handle)
	//	{
	//	}
	//	static LPFN_ACCEPTEX accept_ex;
	//};
	//void _GetAcceptEx(const SOCKET &s, LPFN_ACCEPTEX *ptr)
	//{
	//	GUID id = WSAID_ACCEPTEX;
	//	DWORD buf;
	//	WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &ptr, sizeof(ptr), &buf, NULL, NULL);
	//}
	//void _GetAcceptExSockaddr(const SOCKET &s, LPFN_GETACCEPTEXSOCKADDRS *ptr)
	//{
	//	GUID id = WSAID_GETACCEPTEXSOCKADDRS;
	//	DWORD buf;
	//	WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &ptr, sizeof(ptr), &buf, NULL, NULL);
	//}
#endif //Win32
#ifdef LINUX

#endif //LINUX


}
