#include <url_shortener/tui/table_model.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <url_shortener/tui/text_fit.hpp>

namespace url_shortener::tui
{

namespace
{

///< Lower bound for a page size (`limit`), mirroring the catalog.
constexpr std::size_t k_min_limit = 1;

///< Upper bound for a page size (`limit`), mirroring the catalog.
constexpr std::size_t k_max_limit = 500;

}  // namespace

TableModel::TableModel(std::vector<ColumnDef> columns, std::size_t page_size)
    : m_columns_(std::move(columns))
{
    const std::size_t limit =
        std::min(std::max(page_size, k_min_limit), k_max_limit);
    m_query_.limit = limit;
    m_query_.offset = 0;
}

void TableModel::set_query_handler(QueryHandler handler)
{
    m_handler_ = std::move(handler);
}

void TableModel::emit_query()
{
    ++m_generation_;
    m_state_ = LoadState::loading;
    if (m_handler_)
    {
        m_handler_(m_query_, m_generation_);
    }
}

void TableModel::refresh()
{
    emit_query();
}

bool TableModel::apply_result(std::uint64_t generation, PageResult result)
{
    if (generation != m_generation_)
    {
        return false;
    }

    m_result_ = std::move(result);
    m_error_.clear();

    if (m_result_.rows.empty())
    {
        m_selected_.reset();
        m_state_ =
            m_result_.total == 0 ? LoadState::empty : LoadState::loaded;
    }
    else
    {
        const std::size_t last = m_result_.rows.size() - 1;
        m_selected_ = std::min(m_selected_.value_or(0), last);
        m_state_ = LoadState::loaded;
    }
    return true;
}

void TableModel::apply_error(std::uint64_t generation, std::string message)
{
    if (generation != m_generation_)
    {
        return;
    }
    m_state_ = LoadState::error;
    m_error_ = std::move(message);
}

void TableModel::select_next()
{
    if (m_result_.rows.empty())
    {
        return;
    }
    const std::size_t last = m_result_.rows.size() - 1;
    m_selected_ = std::min(m_selected_.value_or(0) + 1, last);
}

void TableModel::select_prev()
{
    if (m_result_.rows.empty())
    {
        return;
    }
    const std::size_t cur = m_selected_.value_or(0);
    m_selected_ = cur == 0 ? 0 : cur - 1;
}

void TableModel::select_first()
{
    m_selected_ = m_result_.rows.empty()
                      ? std::optional<std::size_t>()
                      : std::optional<std::size_t>(0);
}

void TableModel::select_last()
{
    m_selected_ = m_result_.rows.empty()
                      ? std::optional<std::size_t>()
                      : std::optional<std::size_t>(m_result_.rows.size() - 1);
}

void TableModel::next_page()
{
    if (page_index() + 1 >= page_count())
    {
        return;
    }
    m_query_.offset = (page_index() + 1) * m_query_.limit;
    m_selected_ = 0;
    emit_query();
}

void TableModel::prev_page()
{
    if (page_index() == 0)
    {
        return;
    }
    m_query_.offset = (page_index() - 1) * m_query_.limit;
    m_selected_ = 0;
    emit_query();
}

void TableModel::first_page()
{
    if (m_query_.offset == 0)
    {
        return;
    }
    m_query_.offset = 0;
    m_selected_ = 0;
    emit_query();
}

void TableModel::last_page()
{
    const std::size_t last_offset = (page_count() - 1) * m_query_.limit;
    if (m_query_.offset == last_offset)
    {
        return;
    }
    m_query_.offset = last_offset;
    m_selected_ = 0;
    emit_query();
}

void TableModel::toggle_sort(std::size_t column_index)
{
    if (column_index >= m_columns_.size())
    {
        return;
    }
    const ColumnDef& col = m_columns_[column_index];
    if (!col.sortable)
    {
        return;
    }

    if (m_query_.sort_key == col.key)
    {
        m_query_.descending = !m_query_.descending;
    }
    else
    {
        m_query_.sort_key = col.key;
        m_query_.descending = false;  // first sort of a column is ascending
    }
    m_query_.offset = 0;
    m_selected_ = 0;
    emit_query();
}

void TableModel::set_filter(std::string text)
{
    m_query_.filter = std::move(text);
    m_query_.offset = 0;
    m_selected_ = 0;
    emit_query();
}

void TableModel::toggle_column(std::size_t column_index)
{
    if (column_index >= m_columns_.size())
    {
        return;
    }
    ColumnDef& col = m_columns_[column_index];
    if (col.visible)
    {
        if (visible_indices().size() <= 1)
        {
            return;  // keep at least one column visible
        }
        col.visible = false;
    }
    else
    {
        col.visible = true;
    }
    clamp_window();
}

void TableModel::scroll_columns(int delta)
{
    const std::size_t visible_count = visible_indices().size();
    if (visible_count == 0)
    {
        m_window_start_ = 0;
        return;
    }
    const auto max_start = static_cast<long long>(visible_count - 1);
    long long next = static_cast<long long>(m_window_start_) + delta;
    next = std::max<long long>(0, std::min<long long>(next, max_start));
    m_window_start_ = static_cast<std::size_t>(next);
}

std::vector<std::size_t> TableModel::visible_indices() const
{
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < m_columns_.size(); ++i)
    {
        if (m_columns_[i].visible)
        {
            result.push_back(i);
        }
    }
    return result;
}

void TableModel::clamp_window()
{
    const std::size_t visible_count = visible_indices().size();
    if (visible_count == 0)
    {
        m_window_start_ = 0;
        return;
    }
    if (m_window_start_ > visible_count - 1)
    {
        m_window_start_ = visible_count - 1;
    }
}

int TableModel::desired_width(std::size_t column_index) const
{
    const ColumnDef& col = m_columns_[column_index];
    std::size_t content = display_width(col.title);
    for (const auto& row : m_result_.rows)
    {
        if (column_index < row.size())
        {
            content = std::max(content, display_width(row[column_index]));
        }
    }
    const auto lo = static_cast<std::size_t>(std::max(1, col.min_width));
    const auto hi =
        static_cast<std::size_t>(std::max(col.min_width, col.max_width));
    content = std::min(std::max(content, lo), hi);
    return static_cast<int>(content);
}

std::vector<std::pair<std::size_t, int>>
TableModel::compute_window(int available_width) const
{
    std::vector<std::pair<std::size_t, int>> shown;
    if (available_width <= 0)
    {
        return shown;
    }
    const std::vector<std::size_t> visible = visible_indices();
    if (visible.empty())
    {
        return shown;
    }

    const std::size_t start = std::min(m_window_start_, visible.size() - 1);
    int used = 0;  // columns consumed, including one-space separators
    for (std::size_t k = start; k < visible.size(); ++k)
    {
        const std::size_t ci = visible[k];
        const int want = desired_width(ci);
        if (shown.empty())
        {
            const int w = std::min(want, available_width);
            shown.emplace_back(ci, w);
            used = w;
            continue;
        }
        const int sep = 1;
        const int remaining = available_width - used - sep;
        const int min_w = std::max(1, m_columns_[ci].min_width);
        if (remaining < min_w)
        {
            break;
        }
        const int w = std::min(want, remaining);
        shown.emplace_back(ci, w);
        used += sep + w;
    }
    return shown;
}

std::vector<int> TableModel::layout(int available_width) const
{
    std::vector<int> widths;
    for (const auto& entry : compute_window(available_width))
    {
        widths.push_back(entry.second);
    }
    return widths;
}

std::vector<const ColumnDef*>
TableModel::shown_columns(int available_width) const
{
    std::vector<const ColumnDef*> cols;
    for (const auto& entry : compute_window(available_width))
    {
        cols.push_back(&m_columns_[entry.first]);
    }
    return cols;
}

bool TableModel::overflow_left(int available_width) const
{
    (void)available_width;
    const std::vector<std::size_t> visible = visible_indices();
    if (visible.empty())
    {
        return false;
    }
    return std::min(m_window_start_, visible.size() - 1) > 0;
}

bool TableModel::overflow_right(int available_width) const
{
    const std::vector<std::size_t> visible = visible_indices();
    if (visible.empty())
    {
        return false;
    }
    const std::size_t start = std::min(m_window_start_, visible.size() - 1);
    const std::size_t shown = compute_window(available_width).size();
    return start + shown < visible.size();
}

std::size_t TableModel::page_index() const
{
    return m_query_.limit == 0 ? 0 : m_query_.offset / m_query_.limit;
}

std::size_t TableModel::page_count() const
{
    if (m_query_.limit == 0 || m_result_.total == 0)
    {
        return 1;
    }
    return (m_result_.total + m_query_.limit - 1) / m_query_.limit;
}

std::size_t TableModel::total() const
{
    return m_result_.total;
}

std::optional<std::size_t> TableModel::selected() const
{
    return m_selected_;
}

const std::string* TableModel::selected_raw() const
{
    if (!m_selected_.has_value() ||
        *m_selected_ >= m_result_.raw_rows.size())
    {
        return nullptr;
    }
    return &m_result_.raw_rows[*m_selected_];
}

LoadState TableModel::state() const
{
    return m_state_;
}

const std::string& TableModel::error_message() const
{
    return m_error_;
}

const PageQuery& TableModel::query() const
{
    return m_query_;
}

const std::vector<std::vector<std::string>>& TableModel::rows() const
{
    return m_result_.rows;
}

}  // namespace url_shortener::tui
