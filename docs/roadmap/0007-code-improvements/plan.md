## 01. Doxygen requirements

1. Do not leave small tail on the last line (less than 30% from maximum line size)
2. Do not split method/funcition declaration if it only slightly violates line length
3. One empty line between data members and methods

```
///< Password check for a user, successful or not.
constexpr const char* PasswordVerify = "password.verify";

///< Creation of a new authenticated session token.
constexpr const char* SessionCreate = "session.create";

///< Introspection / validation of an existing session token.
constexpr const char* TokenIntrospect = "token.introspect";
```

4. params must be straight oneliners unless they require in 3 full max-length lines

```
/**
 * @brief Validates the query parameters, queries the repository for
 * aggregate statistics, and writes a JSON response body and HTTP
 * status code to the out-params.
 *
 * Both `body` and `status_code` are always written; the function currently always returns `true`.
 * @param slug Short-link slug to query; a 404 response is produced for an empty slug.
 * @param from Inclusive query start time as a Unix epoch-second integer string.
 * @param to Exclusive query end time as a Unix epoch-second integer string; must be strictly greater than `from`.
 * @param bucket Aggregation granularity: `"hour"`, `"day"`, or `"week"`; a 400 response is produced for any other value.
 * @param body Receives the JSON response body; must not be null. Always written before returning.
 * @param status_code Receives the HTTP status code (200, 400, 404, or 500); must not be null.  Always written before returning.
 * @param request_id Optional request correlation ID embedded in error response bodies for tracing.
 * @return Always `true` in the current implementation.
 */
bool Handle(const std::string& slug,
            const std::string& from,
            const std::string& to,
            const std::string& bucket,
            std::string* body,
            int* status_code,
            std::string request_id = "") const;
```
