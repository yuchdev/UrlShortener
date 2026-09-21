# 01 - Define new command DTOs and service methods

**Parent task:** 01.0 Command layer completion
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 02, 03

## Objective

Add the four DTOs and matching `LinkCommandService` methods needed to cover
`PATCH`, `DELETE`, `enable`/`disable`, `restore`, and `preview` - mirroring
the existing `CreateLinkCommand`/`GetLinkQuery` shape (plain struct in,
`Result<LinkView>` out, `AppError` for expected failures).

## Files to modify

- `include/url_shortener/app/link_command_service.hpp` - add DTOs and method
  declarations.
- `src/app/link_command_service.cpp` - add method implementations.
- `include/url_shortener/app/legacy_adapters.hpp` - extend `ILinkStore`
  usage if `LegacyLinkStore` needs new members (e.g. an `update` capable of
  touching `enabled`/`deleted_at`/`expires_at`/`tags`/`metadata`/`campaign`
  in one call); reuse `ILinkStore::create`/`findBySlug`/`findById` where
  sufficient rather than widening the interface unnecessarily.
- `src/app/legacy_adapters.cpp` - implement any new/changed `LegacyLinkStore`
  members against the same `linkRepository()` singleton
  `getLinkForRead`/`updateLinkAndInvalidateCache` calls the handlers use
  today, so behavior stays provably identical.

## New API (indicative shapes - finalize field-for-field against the current
`handlePatchLink`/`handleLifecycleAction`/`handlePreviewLink` bodies)

```cpp
namespace url_shortener::app {

struct UpdateLinkCommand
{
    std::string slug;
    std::optional<bool> enabled;
    std::optional<std::optional<std::string>> expires_at;  // outer optional = "field present"
    std::optional<std::vector<std::string>> tags;
    std::optional<std::unordered_map<std::string, std::string>> metadata;
    std::optional<std::optional<Link::Campaign>> campaign;
};

struct DeleteLinkCommand
{
    std::string slug;
};

struct SetLinkEnabledCommand
{
    std::string slug;
    bool enabled;
};

struct RestoreLinkCommand
{
    std::string slug;
};

// PreviewLinkQuery can likely reuse GetLinkQuery{GetLinkBy::slug, slug} plus
// a distinct return-shape method rather than a new query DTO - confirm
// against handlePreviewLink's exact field set (slug/url/status/redirect_type/
// enabled/expires_at/deleted_at) before deciding to add a new View type.

class LinkCommandService
{
public:
    // ... existing methods ...
    Result<LinkView> UpdateLink(const UpdateLinkCommand& command) const;
    Result<LinkView> DeleteLink(const DeleteLinkCommand& command) const;
    Result<LinkView> SetLinkEnabled(const SetLinkEnabledCommand& command) const;
    Result<LinkView> RestoreLink(const RestoreLinkCommand& command) const;
    Result<LinkView> PreviewLink(const GetLinkQuery& query) const;
};

} // namespace url_shortener::app
```

The double-`optional` fields on `UpdateLinkCommand` mirror the existing
`hasJsonField(...)` "field present vs. absent vs. explicit null" distinction
`handlePatchLink` already implements for `expires_at`/`campaign` - do not
collapse that distinction while porting it into the DTO, or PATCH semantics
change (an absent field must stay untouched; an explicit `null` must clear
it).

## Tests

Add to `tests/unit/app/`, following the existing numeric-prefix convention
(next available: `13`):
- `13_link_command_service_update_*.cpp` - present/absent/null field
  semantics, invalid `expires_at`, invalid `tags`/`metadata`/`campaign`
  (reuse the same validators `handlePatchLink` calls:
  `validateTags`/`validateMetadata`/`validateCampaign`).
- `14_link_command_service_delete_*.cpp` - soft-delete sets `deleted_at`,
  not-found propagation.
- `15_link_command_service_set_enabled_*.cpp` - enable and disable both
  routed through the one method with `enabled` true/false.
- `16_link_command_service_restore_*.cpp` - clears `deleted_at`, not-found
  propagation.

Register new files in `CMakeLists.txt`'s app unit-test source list.

## Constraints

- No Beast/HTTP type appears in any new DTO or method signature (C2).
- Validation logic (`validateTags`, `validateMetadata`, `validateCampaign`,
  RFC3339 parsing) is reused from `core/utils.h`, not reimplemented.
- New public symbols carry Doxygen comments.

## Success criteria

- [ ] `UpdateLinkCommand`, `DeleteLinkCommand`, `SetLinkEnabledCommand`,
      `RestoreLinkCommand` exist with the field-presence semantics described
      above.
- [ ] `LinkCommandService::UpdateLink/DeleteLink/SetLinkEnabled/RestoreLink/PreviewLink`
      exist and are unit-tested in isolation (no HTTP handler calls them
      yet - that's subtasks 02/03).
- [ ] New unit tests registered and passing under `ctest -L unit`.
