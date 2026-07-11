#include "url_shortener/analytics/ClickEvent.hpp"

namespace url_shortener::analytics {

bool ClickEvent::Validate(AnalyticsError* error) const
{
    const bool status_ok = (status_code >= 300 && status_code <= 399) || status_code == 404 || status_code == 410;
    const bool time_ok = occurred_at != Timestamp{};
    if (event_id.empty() || !time_ok || slug.empty() || domain.empty() || !status_ok) {
        if (error) {
            error->code = AnalyticsErrorCode::validation;
            error->message = "Invalid click event";
        }
        return false;
    }
    return true;
}

} // namespace url_shortener::analytics
