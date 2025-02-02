#include "core/network.h"
#include "core/iallocator.h"
#include "core/string.h"
#include "core/pc/simple_win.h"
#undef WIN32_LEAN_AND_MEAN
#include <Windows.h>


#pragma comment(lib, "Ws2_32.lib")


namespace Lumix
{
namespace Net
{


TCPAcceptor::TCPAcceptor(IAllocator& allocator)
	: m_allocator(allocator)
{
}


TCPAcceptor::~TCPAcceptor()
{
	::closesocket(m_socket);
}


bool TCPAcceptor::start(const char* ip, uint16 port)
{
	WORD sockVer;
	WSADATA wsaData;
	sockVer = MAKEWORD(2, 2);
	if (WSAStartup(sockVer, &wsaData) != 0) return false;

	SOCKET socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET) return false;

	SOCKADDR_IN sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = ip ? ::inet_addr(ip) : INADDR_ANY;

	int retVal = ::bind(socket, (LPSOCKADDR)&sin, sizeof(sin));
	if (retVal == SOCKET_ERROR) return false;

	m_socket = socket;

	return ::listen(socket, 10) == 0;
}


void TCPAcceptor::close(TCPStream* stream)
{
	LUMIX_DELETE(m_allocator, stream);
}


TCPStream* TCPAcceptor::accept()
{
	SOCKET socket = ::accept(m_socket, nullptr, nullptr);
	return LUMIX_NEW(m_allocator, TCPStream)(socket);
}


TCPConnector::TCPConnector(IAllocator& allocator)
	: m_allocator(allocator)
	, m_socket(0)
{
}


TCPConnector::~TCPConnector()
{
	::closesocket(m_socket);
}

TCPStream* TCPConnector::connect(const char* ip, uint16 port)
{
	WORD sockVer;
	WSADATA wsaData;
	sockVer = MAKEWORD(2, 2);
	if (WSAStartup(sockVer, &wsaData) != 0) return nullptr;

	SOCKET socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET) return nullptr;

	SOCKADDR_IN sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = ip ? ::inet_addr(ip) : INADDR_ANY;

	if (::connect(socket, (LPSOCKADDR)&sin, sizeof(sin)) != 0) return nullptr;

	m_socket = socket;
	return LUMIX_NEW(m_allocator, TCPStream)(socket);
}

void TCPConnector::close(TCPStream* stream)
{
	LUMIX_DELETE(m_allocator, stream);
}


TCPStream::~TCPStream()
{
	::closesocket(m_socket);
}


bool TCPStream::readString(char* string, uint32 max_size)
{
	uint32 len = 0;
	bool ret = true;
	ret &= read(len);
	ASSERT(len < max_size);
	ret &= read((void*)string, len);

	return ret;
}


bool TCPStream::writeString(const char* string)
{
	uint32 len = (uint32)stringLength(string) + 1;
	bool ret = write(len);
	ret &= write((const void*)string, len);

	return ret;
}


bool TCPStream::read(void* buffer, size_t size)
{
	int32 to_receive = (int32)size;
	char* ptr = static_cast<char*>(buffer);

	do
	{
		int received = ::recv(m_socket, ptr, to_receive, 0);
		ptr += received;
		to_receive -= received;
		if (received <= 0)
		{
			if (WSAGetLastError() == WSAEWOULDBLOCK)
			{
				ptr -= received;
				to_receive += received;
			}
			else
			{
				return false;
			}
		}
	} while (to_receive > 0);
	return true;
}


bool TCPStream::write(const void* buffer, size_t size)
{
	int send = ::send(m_socket, static_cast<const char*>(buffer), (int)size, 0);
	return (size_t)send == size;
}


} // namespace Net
} // namespace Lumix
