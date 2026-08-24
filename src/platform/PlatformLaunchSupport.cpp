#include "../../src/platform/PlatformLaunchSupport.hpp"

PlatformLaunchSupport::PlatformLaunchSupport()
{
}

PlatformLaunchSupport::PlatformLaunchSupport(const PlatformLaunchSupport &other)
{
	(void)other;
}

PlatformLaunchSupport::~PlatformLaunchSupport()
{
}

PlatformLaunchSupport &PlatformLaunchSupport::operator=(const PlatformLaunchSupport &other)
{
	(void)other;
	return (*this);
}

#if defined(__APPLE__)

ft_dumb_keyboard_layout PlatformLaunchSupport::detect_system_layout()
{
	CFStringRef			id_ref;
	TISInputSourceRef	src;
	bool				is_azerty;
	char				buf[128] = {};

	id_ref = nullptr;
	src = TISCopyCurrentKeyboardLayoutInputSource();
	is_azerty = false;
	if (src)
		id_ref = static_cast<CFStringRef>(TISGetInputSourceProperty(src,
					kTISPropertyInputSourceID));
	if (id_ref)
	{
		CFStringGetCString(id_ref, buf, sizeof(buf), kCFStringEncodingUTF8);
		is_azerty = (std::strstr(buf, "French") != nullptr || std::strstr(buf,
					"AZERTY") != nullptr || std::strstr(buf,
					"azerty") != nullptr);
	}
	if (src)
		CFRelease(src);
	if (is_azerty)
		return (FT_DUMB_KEYBOARD_LAYOUT_AZERTY);
	return (FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
}

#elif defined(_WIN32)

ft_dumb_keyboard_layout PlatformLaunchSupport::detect_system_layout()
{
	return (FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
}

#else

ft_dumb_keyboard_layout PlatformLaunchSupport::detect_system_layout()
{
	const char	*lang = std::getenv("LANG");

	if (!lang)
		lang = std::getenv("LC_ALL");
	if (!lang)
		lang = std::getenv("LC_MESSAGES");
	if (lang && std::strncmp(lang, "fr_", 3) == 0)
		return (FT_DUMB_KEYBOARD_LAYOUT_AZERTY);
	return (FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
}

#endif

#if defined(_WIN32)

void PlatformLaunchSupport::wait_for_escape_release()
{
	while ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
		Sleep(16);
}

#else

void PlatformLaunchSupport::wait_for_escape_release()
{
}

#endif

#if defined(_WIN32)

void PlatformLaunchSupport::clear_pending_quit_messages()
{
	MSG	msg;

	while (PeekMessageA(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != 0)
	{
	}
}

#else

void PlatformLaunchSupport::clear_pending_quit_messages()
{
}

#endif
