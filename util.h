
#include <type_traits>
#include <memory>
#include <iomanip>
#include <string>

namespace sab
{

	template<typename T>
	using HandleGuard_t = std::shared_ptr<typename std::remove_pointer<T>::type>;

	template<typename T, typename U>
	auto HandleGuard(T handle, U deleter)
	{
		return HandleGuard_t<T>(handle, deleter);
	}

	std::wstring FormatLastError();
	std::wstring FormatLastError(int errorCode);
	
}

#define LogLastError L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, GetLastError(), L": ", sab::FormatLastError()