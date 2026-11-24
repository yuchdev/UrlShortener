/**
 * @file redirect_fast_path_guard.cpp
 * @brief Advisory redirect fast-path timing guard (Task 07.0 subtask 32).
 *
 * Measures in-process `GET /{slug}` and `GET /r/{slug}` dispatch through the
 * REAL application router (`RouterBuilder::buildApplicationRouter`) for the
 * cache-hit and cache-miss+repository-hit cases, normalises every number by a
 * CPU calibration loop, and compares against a checked-in baseline. It also
 * proves that merely constructing the command registry, a full set of
 * documented services and an idle `JobRunner` in the same process does not slow
 * the redirect fast path (milestone contract C10).
 *
 * This is a standalone program (its own `main`, no Boost.Test) registered under
 * the CTest `perf` label, which is deliberately kept out of the `unit` gate and
 * the `-L "unit|contract|integration|e2e"` union (docs/testing/README.md 3.5).
 * It is **advisory**: it prints a markdown table and exits 0 on a regression
 * unless `URLSHORTENER_PERF_GATE=1` promotes the baseline check to a hard gate.
 * The structural guard (subtask 31) is the real gate; this one tracks trends.
 *
 * Measurement happens only in an optimised build. A build without `NDEBUG`
 * prints `skipped: not an optimized build` and exits 0, because Debug numbers
 * are not comparable to the Release baseline. Wall-clock measurement is allowed
 * here, and only here, because the file lives under the advisory `perf` label.
 *
 * Baseline: `tests/perf/redirect/baseline.json`, located at build time through
 * the `PERF_BASELINE_PATH` compile definition. It is written only by a human
 * running `--write-baseline` on a quiet Release machine and is never touched by
 * CI. See `tests/perf/README.md` ("Baseline update procedure") for the steps.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/builtin_commands.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/handler_types.hpp>
#include <url_shortener/http/router.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace
{

namespace bhttp = boost::beast::http;

using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::http::BeastRequest;
using url_shortener::http::Router;
using url_shortener::http::RouterBuilder;

/// @brief Number of links pre-created in the repository so the hash maps are
/// realistically sized during measurement.
constexpr std::size_t kDatasetSize = 10000;

/// @brief Number of distinct slugs the measured requests cycle through, so no
/// single hot entry dominates the cache behaviour.
constexpr std::size_t kDistinctSlugs = 1000;

/// @brief Requests timed together in one sample to beat `steady_clock`
/// resolution; the recorded per-request figure is the batch divided by this.
constexpr std::size_t kBatch = 64;

/// @brief Warm-up dispatches discarded before measurement (JIT-free C++, but
/// this still fills caches/branch predictors and the slug hash maps).
constexpr std::size_t kWarmupIterations = 20000;

/// @brief Measured dispatches per scenario (a multiple of @ref kBatch).
constexpr std::size_t kMeasuredIterations = 200000;

/// @brief Repetitions of the whole measurement for the self-spread check.
constexpr std::size_t kRepetitions = 3;

/// @brief Relative spread (stddev/median) above which a run is flagged
/// `UNRELIABLE` (and still exits 0).
constexpr double kUnreliableSpread = 0.15;

/// @brief Allowed normalised p50 growth over baseline before C-baseline fails.
constexpr double kP50Tolerance = 0.25;

/// @brief Allowed normalised p99 growth over baseline before C-baseline fails.
constexpr double kP99Tolerance = 0.40;

/// @brief Allowed p50 ratio for the registry-presence check (<= +10 %).
constexpr double kRegistryRatioLimit = 1.10;

/**
 * @brief One scenario's measured latency distribution.
 */
struct Sample
{
    ///< 50th percentile of per-request dispatch latency, in nanoseconds.
    double p50_ns = 0.0;

    ///< 95th percentile of per-request dispatch latency, in nanoseconds.
    double p95_ns = 0.0;

    ///< 99th percentile of per-request dispatch latency, in nanoseconds.
    double p99_ns = 0.0;

    ///< Number of measured requests that produced this distribution.
    std::size_t n = 0;
};

/**
 * @brief A parsed baseline file: the machine calibration it was recorded with
 * and a per-scenario expected distribution.
 */
struct Baseline
{
    ///< `true` once a baseline file was found and parsed; `false` disables the
    ///< C-baseline comparison (treated as advisory "no baseline").
    bool loaded = false;

    ///< Calibration nanoseconds recorded alongside the baseline, used to
    ///< renormalise the stored figures onto this machine.
    double calibration_ns = 0.0;

    ///< Expected distribution per scenario name.
    std::map<std::string, Sample> scenarios;
};

/**
 * @brief Returns the @p pct percentile (0..100) of @p values using
 * nearest-rank.
 * @param values Latency samples; copied and sorted internally. Must be
 * non-empty (the caller guarantees at least one sample per scenario).
 * @param pct Percentile in the inclusive range [0, 100].
 * @return The nearest-rank percentile value in nanoseconds.
 */
double percentile(std::vector<double> values, double pct)
{
    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values.front();
    }
    const double rank = (pct / 100.0) * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(rank));
    const auto hi = static_cast<std::size_t>(std::ceil(rank));
    const double frac = rank - static_cast<double>(lo);
    return values[lo] + (values[hi] - values[lo]) * frac;
}

/**
 * @brief Returns the median of @p values (nearest-rank p50).
 * @param values Non-empty sample vector; copied internally.
 * @return The median value.
 */
double median(std::vector<double> values)
{
    return percentile(std::move(values), 50.0);
}

/**
 * @brief Runs a fixed integer/hash loop and returns its per-iteration cost in
 * nanoseconds, as the median of five runs.
 *
 * The loop touches no allocation, I/O or syscall, so its cost tracks raw CPU
 * speed. Dividing every measured latency by this figure yields a machine-
 * independent "normalised" number the baseline comparison uses, so a faster or
 * slower runner does not by itself trip the guard.
 *
 * @return Median per-iteration cost of the calibration loop, in nanoseconds;
 * always positive on any real machine.
 */
double calibrationNs()
{
    constexpr std::size_t iters = 4000000;
    std::vector<double> runs;
    runs.reserve(5);
    for (int run = 0; run < 5; ++run) {
        std::uint64_t acc =
            0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(run);
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iters; ++i) {
            acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
            acc ^= acc >> 31;
        }
        const auto t1 = std::chrono::steady_clock::now();
        // Publish acc so the loop cannot be optimised away.
        static volatile std::uint64_t sink = 0;
        sink = acc;
        const double ns = std::chrono::duration_cast<
                              std::chrono::duration<double, std::nano>>(t1 - t0)
                              .count();
        runs.push_back(ns / static_cast<double>(iters));
    }
    return median(std::move(runs));
}

/**
 * @brief Builds a GET request for @p target with a Host header set.
 * @param target Request target (path, optionally with query string).
 * @return A prepared GET request suitable for `Router::dispatch`.
 */
BeastRequest makeGet(const std::string& target)
{
    BeastRequest req {bhttp::verb::get, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.prepare_payload();
    return req;
}

/**
 * @brief Builds an active link for @p slug.
 * @param slug Slug for the link.
 * @param redirect_type Redirect semantics (temporary -> 302, permanent -> 301).
 * @return A populated active link.
 */
Link makeLink(const std::string& slug,
              const RedirectType redirect_type = RedirectType::temporary)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = redirect_type;
    return link;
}

/**
 * @brief Minimal dispatch config: private targets allowed, analytics toggled by
 * @p analytics so the enabled-vs-disabled comparison can be measured.
 * @param analytics Value for `ServerConfig::analytics_enabled`.
 * @return A `ServerConfig` for `Router::dispatch`.
 */
ServerConfig makeConfig(bool analytics)
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.analytics_enabled = analytics;
    return config;
}

/// @brief Sink consumed after every dispatch so the optimiser cannot elide the
/// call; read is never asserted on (timing only).
volatile unsigned g_response_sink = 0;

/**
 * @brief Pre-created, pre-serialised request corpus for one scenario.
 */
struct Corpus
{
    ///< One prepared request per distinct slug, cycled during measurement.
    std::vector<BeastRequest> requests;

    ///< The slug behind each request, for cache eviction in the miss scenarios.
    std::vector<std::string> slugs;
};

/**
 * @brief Builds the request corpus for a path kind.
 * @param prefixed `true` for `GET /r/{slug}`, `false` for `GET /{slug}`.
 * @param slug_prefix Slug prefix identifying the scenario's slug namespace.
 * @return A corpus of @ref kDistinctSlugs prepared requests.
 */
Corpus buildCorpus(bool prefixed, const std::string& slug_prefix)
{
    Corpus corpus;
    corpus.requests.reserve(kDistinctSlugs);
    corpus.slugs.reserve(kDistinctSlugs);
    for (std::size_t i = 0; i < kDistinctSlugs; ++i) {
        std::ostringstream slug;
        slug << slug_prefix << std::setw(6) << std::setfill('0') << i;
        const std::string target = (prefixed ? "/r/" : "/") + slug.str();
        corpus.requests.push_back(makeGet(target));
        corpus.slugs.push_back(slug.str());
    }
    return corpus;
}

/**
 * @brief Measures one scenario and returns its latency distribution.
 *
 * Times batches of @ref kBatch dispatches through @p router. For the cache-miss
 * scenarios the batch's slugs are evicted from the cache *outside* the timed
 * region, so every timed dispatch is a genuine cache miss that falls back to
 * the repository. For the cache-hit scenarios the batch's slugs are instead
 * re-warmed into the cache outside the timed region before each batch: the
 * default `LegacyLinkStore::recordRedirect` invalidates a slug's cache entry
 * after every successful redirect, so without the re-warm a "hit" scenario
 * would silently decay into misses. Re-warming keeps the hit scenario a true
 * warm-cache measurement (a no-op for `root_404`, whose slugs resolve nowhere).
 *
 * @param router The real application router under test.
 * @param config Dispatch config (controls analytics).
 * @param corpus Pre-built requests/slugs for the scenario.
 * @param cache_hit `true` to re-warm each batch's slugs before timing (warm-
 * cache dispatch); `false` to evict them so the repository is consulted.
 * @return The measured @ref Sample (p50/p95/p99 and the sample count).
 */
Sample measureScenario(const Router& router,
                       const ServerConfig& config,
                       const Corpus& corpus,
                       bool cache_hit)
{
    // Warm up (fills branch predictors and the slug hash maps).
    std::size_t idx = 0;
    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        const auto res = router.dispatch(corpus.requests[idx], config, false);
        g_response_sink = res.result_int();
        idx = (idx + 1) % corpus.requests.size();
    }

    std::vector<double> per_request;
    per_request.reserve(kMeasuredIterations);
    idx = 0;
    const std::size_t samples = kMeasuredIterations / kBatch;
    for (std::size_t s = 0; s < samples; ++s) {
        for (std::size_t b = 0; b < kBatch; ++b) {
            const std::size_t j = (idx + b) % corpus.requests.size();
            if (cache_hit) {
                const auto link =
                    url_shortener::linkRepository().getBySlug(corpus.slugs[j]);
                if (link.has_value()) {
                    url_shortener::linkCache().set(corpus.slugs[j], *link);
                }
            }
            else {
                url_shortener::linkCache().erase(corpus.slugs[j]);
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < kBatch; ++b) {
            const auto res =
                router.dispatch(corpus.requests[idx], config, false);
            g_response_sink = res.result_int();
            idx = (idx + 1) % corpus.requests.size();
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration_cast<
                              std::chrono::duration<double, std::nano>>(t1 - t0)
                              .count();
        per_request.push_back(ns / static_cast<double>(kBatch));
    }

    Sample sample;
    sample.p50_ns = percentile(per_request, 50.0);
    sample.p95_ns = percentile(per_request, 95.0);
    sample.p99_ns = percentile(per_request, 99.0);
    sample.n = per_request.size();
    return sample;
}

/**
 * @brief Seeds @ref kDatasetSize filler links plus every corpus slug into the
 * repository, so the hash maps are realistically sized and the corpus slugs
 * resolve to active links. Cache warming is handled per batch in
 * @ref measureScenario, not here.
 * @param corpora The scenario corpora whose slugs must resolve to active links.
 */
void seedDataset(const std::vector<Corpus>& corpora)
{
    // Bulk filler so the repository hash maps are realistically sized.
    for (std::size_t i = 0; i < kDatasetSize; ++i) {
        std::ostringstream slug;
        slug << "fill" << std::setw(6) << std::setfill('0') << i;
        (void)url_shortener::linkRepository().create(makeLink(slug.str()));
    }
    for (const auto& corpus : corpora) {
        for (std::size_t i = 0; i < corpus.slugs.size(); ++i) {
            const auto type = (i % 2 == 0) ? RedirectType::temporary
                                           : RedirectType::permanent;
            const auto link = makeLink(corpus.slugs[i], type);
            (void)url_shortener::linkRepository().create(link);
        }
    }
}

/**
 * @brief Finds the next number after the first occurrence of `"key"` in @p
 * text.
 * @param text The whole baseline JSON text.
 * @param from Offset to begin the search at.
 * @param key JSON key to locate (without quotes).
 * @param[out] value Receives the parsed number on success.
 * @return Offset just past the parsed number on success, or
 * `std::string::npos` when the key or a number after it is not found.
 */
std::size_t findNumber(const std::string& text,
                       std::size_t from,
                       const std::string& key,
                       double& value)
{
    const std::string needle = "\"" + key + "\"";
    const auto key_pos = text.find(needle, from);
    if (key_pos == std::string::npos) {
        return std::string::npos;
    }
    auto p = text.find(':', key_pos + needle.size());
    if (p == std::string::npos) {
        return std::string::npos;
    }
    ++p;
    while (p < text.size()
           && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n'
               || text[p] == '\r'))
    {
        ++p;
    }
    const std::size_t start = p;
    while (p < text.size()
           && (std::isdigit(static_cast<unsigned char>(text[p])) != 0
               || text[p] == '.' || text[p] == '-' || text[p] == '+'
               || text[p] == 'e' || text[p] == 'E'))
    {
        ++p;
    }
    if (p == start) {
        return std::string::npos;
    }
    value = std::strtod(text.substr(start, p - start).c_str(), nullptr);
    return p;
}

/**
 * @brief Loads and parses the baseline file at @p path.
 *
 * Uses a tiny hand parser (no JSON library, per CLAUDE.md) that locates
 * `calibration_ns` and, for each known scenario, the `p50_ns`/`p95_ns`/`p99_ns`
 * numbers after the scenario's key. A missing or unparseable file yields a
 * `Baseline` with `loaded == false`, which turns the C-baseline check advisory
 * ("no baseline") rather than failing.
 *
 * @param path Filesystem path to the baseline JSON, or `nullptr`.
 * @param scenario_names Scenario keys to extract.
 * @return The parsed baseline; `loaded == false` when it could not be read.
 */
Baseline loadBaseline(const char* path,
                      const std::vector<std::string>& scenario_names)
{
    Baseline baseline;
    if (path == nullptr) {
        return baseline;
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        return baseline;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    if (findNumber(text, 0, "calibration_ns", baseline.calibration_ns)
        == std::string::npos)
    {
        return baseline;
    }
    for (const auto& name : scenario_names) {
        const std::string needle = "\"" + name + "\"";
        const auto at = text.find(needle);
        if (at == std::string::npos) {
            continue;
        }
        Sample sample;
        double n_value = 0.0;
        findNumber(text, at, "p50_ns", sample.p50_ns);
        findNumber(text, at, "p95_ns", sample.p95_ns);
        findNumber(text, at, "p99_ns", sample.p99_ns);
        if (findNumber(text, at, "n", n_value) != std::string::npos) {
            sample.n = static_cast<std::size_t>(n_value);
        }
        baseline.scenarios[name] = sample;
    }
    baseline.loaded = baseline.calibration_ns > 0.0;
    return baseline;
}

/**
 * @brief Constructs a full command registry and an idle 4-worker `JobRunner` in
 * this process, holding them alive for the caller's follow-up measurement.
 *
 * This proves the C-registry-presence check: redirect latency must not change
 * just because the unified command model's objects exist in the address space.
 * The registry is populated with the documented command set (placeholder
 * services that are never executed) and the runner is left idle. The clock is a
 * stack `SystemClock` owned by the caller, so the runner (which holds the clock
 * by reference) must not outlive it.
 */
struct RegistryPresence
{
    ///< Documented command catalog; constructed but never executed here.
    url_shortener::commands::CommandRegistry registry;

    ///< Idle four-worker job runner; threads spin waiting, submitting nothing.
    url_shortener::commands::JobRunner runner;

    /**
     * @brief Builds the registry and starts the idle runner.
     * @param clock Time source held by reference by @ref runner; must outlive
     * this object (the caller keeps it on the stack).
     */
    explicit RegistryPresence(const IClock& clock)
        : runner(clock, url_shortener::commands::JobRunnerOptions {4, 16, 256})
    {
        url_shortener::commands::register_documented_commands(registry);
    }
};

/**
 * @brief One measurement pass across all scenarios.
 * @param router The real application router.
 * @param corpora Scenario corpora in a fixed order matching @p names.
 * @param names Scenario names in corpus order.
 * @param cache_hit Per-scenario cache-hit flags.
 * @param analytics Whether analytics is enabled for this pass.
 * @return Scenario name -> measured sample.
 */
std::map<std::string, Sample> runPass(const Router& router,
                                      const std::vector<Corpus>& corpora,
                                      const std::vector<std::string>& names,
                                      const std::vector<bool>& cache_hit,
                                      bool analytics)
{
    const auto config = makeConfig(analytics);
    std::map<std::string, Sample> out;
    for (std::size_t i = 0; i < corpora.size(); ++i) {
        out[names[i]] =
            measureScenario(router, config, corpora[i], cache_hit[i]);
    }
    return out;
}

/**
 * @brief Prints the markdown results table to stdout (and so to a workflow
 * `GITHUB_STEP_SUMMARY` when redirected).
 * @param pass Measured samples by scenario.
 * @param calibration Calibration nanoseconds for normalisation.
 * @param baseline Parsed baseline (may be unloaded).
 */
void printTable(const std::map<std::string, Sample>& pass,
                double calibration,
                const Baseline& baseline)
{
    std::cout
        << "| scenario | p50 ns | p95 ns | p99 ns | norm p50 | norm p99 | "
           "baseline norm p99 | delta |\n";
    std::cout << "|---|---|---|---|---|---|---|---|\n";
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& [name, sample] : pass) {
        const double norm_p50 = sample.p50_ns / calibration;
        const double norm_p99 = sample.p99_ns / calibration;
        std::cout << "| " << name << " | " << sample.p50_ns << " | "
                  << sample.p95_ns << " | " << sample.p99_ns << " | "
                  << norm_p50 << " | " << norm_p99 << " | ";
        const auto it = baseline.scenarios.find(name);
        if (baseline.loaded && it != baseline.scenarios.end()
            && baseline.calibration_ns > 0.0)
        {
            const double base_norm_p99 =
                it->second.p99_ns / baseline.calibration_ns;
            const double delta = base_norm_p99 > 0.0
                ? (norm_p99 - base_norm_p99) / base_norm_p99
                : 0.0;
            std::cout << base_norm_p99 << " | " << (delta * 100.0) << "% |\n";
        }
        else {
            std::cout << "n/a | n/a |\n";
        }
    }
}

}  // namespace

/**
 * @brief Entry point: skips on Debug, otherwise measures, prints the table and
 * applies the advisory checks.
 *
 * Exit code is 0 in every advisory situation (no baseline, an unreliable run, a
 * regression without the gate). It is non-zero only when
 * `URLSHORTENER_PERF_GATE=1` is set and the C-baseline or C-registry-presence
 * check fails on a reliable run.
 *
 * @return 0 on success/advisory; 1 on a gated check failure.
 */
int main(int argc, char** argv)
{
#ifndef NDEBUG
    (void)argc;
    (void)argv;
    std::cout << "skipped: not an optimized build\n";
    return 0;
#else
    const bool write_baseline =
        argc > 1 && std::strcmp(argv[1], "--write-baseline") == 0;
    const char* ci = std::getenv("CI");
    if (write_baseline && ci != nullptr && std::strcmp(ci, "true") == 0) {
        std::cout << "refusing to write baseline under CI\n";
        return 0;
    }
    const bool gate = []()
    {
        const char* v = std::getenv("URLSHORTENER_PERF_GATE");
        return v != nullptr && std::strcmp(v, "1") == 0;
    }();

    const std::vector<std::string> names = {"root_cache_hit",
                                            "root_cache_miss_repo_hit",
                                            "prefixed_cache_hit",
                                            "prefixed_cache_miss_repo_hit",
                                            "root_404"};
    const std::vector<bool> cache_hit = {true, false, true, false, true};

    std::vector<Corpus> corpora;
    corpora.push_back(buildCorpus(false, "rch"));
    corpora.push_back(buildCorpus(false, "rcm"));
    corpora.push_back(buildCorpus(true, "pch"));
    corpora.push_back(buildCorpus(true, "pcm"));
    corpora.push_back(
        buildCorpus(false, "r404"));  // slugs intentionally unseeded

    // Seed every corpus except root_404 so those slugs resolve to 404.
    std::vector<Corpus> seeded(corpora.begin(), corpora.begin() + 4);
    seedDataset(seeded);

    const Router router = RouterBuilder::buildApplicationRouter();
    const double calibration = calibrationNs();

    // C-no-regression-vs-self: repeat the whole pass, take the median of
    // medians of the first scenario and report the relative spread.
    std::vector<std::map<std::string, Sample>> passes;
    std::vector<double> anchor_p50;
    for (std::size_t r = 0; r < kRepetitions; ++r) {
        auto pass = runPass(router, corpora, names, cache_hit, false);
        anchor_p50.push_back(pass[names[0]].p50_ns);
        passes.push_back(std::move(pass));
    }

    const double anchor_median = median(anchor_p50);
    double variance = 0.0;
    for (const double v : anchor_p50) {
        variance += (v - anchor_median) * (v - anchor_median);
    }
    const double stddev =
        std::sqrt(variance / static_cast<double>(anchor_p50.size()));
    const double spread = anchor_median > 0.0 ? stddev / anchor_median : 0.0;
    const bool unreliable = spread > kUnreliableSpread;

    // Report the middle pass (median of medians) for the table.
    const auto& report_pass = passes[passes.size() / 2];

    const Baseline baseline = loadBaseline(
#    ifdef PERF_BASELINE_PATH
        PERF_BASELINE_PATH,
#    else
        nullptr,
#    endif
        names);

    std::cout << "# redirect fast-path timing guard\n";
    std::cout << "calibration_ns = " << std::fixed << std::setprecision(3)
              << calibration << "\n";
    std::cout << "self-spread = " << std::setprecision(2) << (spread * 100.0)
              << "% (" << (unreliable ? "UNRELIABLE" : "reliable") << ")\n";
    std::cout << "baseline = " << (baseline.loaded ? "loaded" : "absent")
              << ", gate = " << (gate ? "on" : "off") << "\n\n";
    std::cout << "## analytics disabled\n";
    printTable(report_pass, calibration, baseline);

    // Second pass with analytics enabled, to track the enabled-vs-disabled
    // comparison documented in the perf methodology.
    std::cout << "\n## analytics enabled\n";
    const auto analytics_pass =
        runPass(router, corpora, names, cache_hit, true);
    printTable(analytics_pass, calibration, baseline);

    // C-registry-presence: measure the anchor scenario before and after
    // constructing the registry + idle JobRunner.
    const auto before =
        measureScenario(router, makeConfig(false), corpora[0], cache_hit[0]);
    SystemClock clock;
    double registry_ratio = 1.0;
    {
        RegistryPresence presence(clock);
        const auto after = measureScenario(
            router, makeConfig(false), corpora[0], cache_hit[0]);
        registry_ratio =
            before.p50_ns > 0.0 ? after.p50_ns / before.p50_ns : 1.0;
    }
    std::cout << "\n## C-registry-presence\n";
    std::cout << "anchor p50 ratio (with registry / without) = "
              << std::setprecision(3) << registry_ratio << " (limit "
              << kRegistryRatioLimit << ")\n";

    if (write_baseline) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(3);
        out << "{ \"recorded\": \"" << url_shortener::currentTimestamp()
            << "\", \"compiler\": \"unknown\", \"cpu\": \"unknown\", "
            << "\"calibration_ns\": " << calibration << ",\n"
            << "  \"scenarios\": {\n";
        bool first = true;
        for (const auto& [name, sample] : report_pass) {
            if (!first) {
                out << ",\n";
            }
            first = false;
            out << "    \"" << name << "\": {\"p50_ns\": " << sample.p50_ns
                << ", \"p95_ns\": " << sample.p95_ns
                << ", \"p99_ns\": " << sample.p99_ns << ", \"n\": " << sample.n
                << "}";
        }
        out << "\n  }\n}\n";
#    ifdef PERF_BASELINE_PATH
        std::ofstream file(PERF_BASELINE_PATH);
        if (file.is_open()) {
            file << out.str();
            std::cout << "\nwrote baseline to " << PERF_BASELINE_PATH << "\n";
        }
#    endif
        return 0;
    }

    // Apply checks. Advisory unless the gate is on; an unreliable run never
    // fails even under the gate (noise, not a regression).
    bool gate_failure = false;
    if (baseline.loaded && !unreliable && baseline.calibration_ns > 0.0) {
        for (const auto& name : names) {
            const auto it = baseline.scenarios.find(name);
            if (it == baseline.scenarios.end() || it->second.p50_ns <= 0.0) {
                continue;
            }
            const double norm_p50 = report_pass.at(name).p50_ns / calibration;
            const double norm_p99 = report_pass.at(name).p99_ns / calibration;
            const double base_p50 = it->second.p50_ns / baseline.calibration_ns;
            const double base_p99 = it->second.p99_ns / baseline.calibration_ns;
            if (norm_p50 > base_p50 * (1.0 + kP50Tolerance)
                || norm_p99 > base_p99 * (1.0 + kP99Tolerance))
            {
                std::cout << "C-baseline regression in " << name << "\n";
                gate_failure = true;
            }
        }
    }
    if (!unreliable && registry_ratio > kRegistryRatioLimit) {
        std::cout << "C-registry-presence regression: ratio " << registry_ratio
                  << " > " << kRegistryRatioLimit << "\n";
        gate_failure = true;
    }

    if (gate && gate_failure) {
        std::cout << "FAIL (gate enabled)\n";
        return 1;
    }
    std::cout << "OK (advisory)\n";
    return 0;
#endif
}
