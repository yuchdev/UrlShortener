#define BOOST_TEST_MODULE TuiFrameModelTest

#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/frame_model.hpp>
#include <url_shortener/tui/screen_id.hpp>

using url_shortener::tui::FrameModel;
using url_shortener::tui::ScreenId;
using url_shortener::tui::SessionInfo;

namespace
{

SessionInfo admin_session()
{
    SessionInfo session;
    session.principal = "local-operator";
    session.read_only = false;
    return session;
}

SessionInfo user_session()
{
    SessionInfo session = admin_session();
    session.read_only = true;
    return session;
}

}  // namespace

// ---------------------------------------------------------------------------
// The title shows the backend label and the Admin role for a read/write session.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(title_for_admin)
{
    FrameModel model(admin_session(), "sqlite (links.db)", true, "dark");
    BOOST_TEST(model.title()
               == "url_shortener -- backend: sqlite (links.db) -- user: "
                  "local/Admin");
}

// ---------------------------------------------------------------------------
// A read-only session shows the User role instead.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(title_for_read_only_user)
{
    FrameModel model(user_session(), "sqlite (links.db)", true, "dark");
    BOOST_TEST(model.title().find("user: local/User") != std::string::npos);
    BOOST_TEST(model.title().find("Admin") == std::string::npos);
}

// ---------------------------------------------------------------------------
// The banner appears only when the backend is not persistent.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(banner_only_when_not_persistent)
{
    FrameModel persistent(admin_session(), "sqlite", true, "dark");
    BOOST_TEST(!persistent.banner().has_value());

    FrameModel memory(admin_session(), "memory", false, "dark");
    BOOST_REQUIRE(memory.banner().has_value());
    BOOST_TEST(*memory.banner() == "memory store - data is lost on exit");
}

// ---------------------------------------------------------------------------
// The tab bar lists the seven numbered tabs plus the palette, exactly one of
// which is active, following the ScreenId passed in.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tabs_have_seven_plus_palette_with_active_flag)
{
    FrameModel model(admin_session(), "sqlite", true, "dark");
    const std::vector<std::pair<std::string, bool>> tabs =
        model.tabs(ScreenId::tables);

    BOOST_REQUIRE(tabs.size() == 8u);
    BOOST_TEST(tabs.front().first == "[1]Dashboard");
    BOOST_TEST(tabs.back().first == "[:]Palette");

    int active_count = 0;
    for (const std::pair<std::string, bool>& tab : tabs)
    {
        if (tab.second)
        {
            ++active_count;
            BOOST_TEST(tab.first == "[3]Tables");  // ScreenId::tables
        }
    }
    BOOST_TEST(active_count == 1);
}

// ---------------------------------------------------------------------------
// A tab with no matching active ScreenId highlights nothing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tabs_highlight_nothing_for_a_nested_only_screen)
{
    FrameModel model(admin_session(), "sqlite", true, "dark");
    // row_json is not a tab; nothing should be highlighted.
    for (const std::pair<std::string, bool>& tab :
         model.tabs(ScreenId::row_json))
    {
        BOOST_TEST(!tab.second);
    }
}

// ---------------------------------------------------------------------------
// A backend label longer than 60 chars is truncated with a trailing "...".
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(long_backend_label_truncated_at_60_chars)
{
    const std::string long_label(100, 'x');
    FrameModel model(admin_session(), long_label, true, "dark");
    const std::string title = model.title();

    // The label segment sits between "backend: " and " -- user:".
    const std::string prefix = "url_shortener -- backend: ";
    const std::string suffix = " -- user: local/Admin";
    BOOST_REQUIRE(title.size() > prefix.size() + suffix.size());
    const std::string label =
        title.substr(prefix.size(),
                     title.size() - prefix.size() - suffix.size());

    BOOST_TEST(label.size() == 60u);
    BOOST_TEST(label.substr(label.size() - 3) == "...");
    BOOST_TEST(label.substr(0, 57) == std::string(57, 'x'));
}
