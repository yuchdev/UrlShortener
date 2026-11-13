#pragma once

#include <string>
#include <string_view>

#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>

namespace url_shortener::commands
{

/**
 * @brief Maps a @ref Permission to its stable catalog wire name.
 *
 * The returned names (`"read"`, `"write"`, `"migrate"`, `"manage_users"`) are
 * the public catalog vocabulary consumed by `command.list`, the generated docs
 * catalog and client codegen; they are deliberately the catalog spelling and
 * not the C++ enumerator identifiers (which happen to coincide here).
 *
 * @param p Any @ref Permission enumerator.
 * @return A view into static storage valid for the program lifetime. Every
 * enumerator maps to a distinct, non-empty name; a value outside the enumerator
 * set (only producible by casting) maps to `"read"`.
 */
std::string_view permission_name(Permission p);

/**
 * @brief Maps an @ref ExecutionKind to its stable catalog wire name.
 *
 * @param k Any @ref ExecutionKind enumerator.
 * @return A view into static storage valid for the program lifetime: `"sync"`
 * or `"job"`. A value outside the enumerator set (only producible by casting)
 * maps to `"sync"`.
 */
std::string_view kind_name(ExecutionKind k);

/**
 * @brief Maps an @ref ArgType to its catalog type name.
 *
 * Returns the catalog vocabulary (ADR 0002 section 2), not the C++ enumerator
 * identifiers: `"string"`, `"int"`, `"bool"`, `"enum"`, `"timestamp"`,
 * `"list<string>"`, `"map<string,string>"`, `"object"`.
 *
 * @param t Any @ref ArgType enumerator.
 * @return A view into static storage valid for the program lifetime. Every
 * enumerator maps to a distinct, non-empty name; a value outside the enumerator
 * set (only producible by casting) maps to `"string"`.
 */
std::string_view arg_type_name(ArgType t);

/**
 * @brief Serializes one @ref CommandDescriptor to its public catalog JSON
 * object.
 *
 * Emits exactly the public catalog shape in a fixed key order, so the output is
 * byte-identical across calls and runs:
 * `{"name","summary","perm","kind","mutating","dry_run","args":[...]}`, where
 * each entry of `args` is
 * `{"name","type","required"[,"enum":[...]][,"default":<token>],"doc"}`,
 * preserving the descriptor's @ref ArgSpec order. `"perm"` is
 * @ref permission_name, `"kind"` is @ref kind_name, `"dry_run"` reflects
 * @ref CommandDescriptor::supports_dry_run, `"type"` is @ref arg_type_name,
 * `"enum"` is present only for an @ref ArgType::enumeration argument, and
 * `"default"` is present only when @ref ArgSpec::default_value is set and is
 * embedded verbatim as the descriptor's trusted raw JSON token.
 *
 * Security: only this public shape is serialized. The handler/factory
 * callables, @ref CommandDescriptor::permission_for, @ref
 * CommandDescriptor::maps_to and every other internal field are never emitted
 * (threat model T-20). All descriptor-supplied strings are trusted but are
 * still escaped through `url_shortener::jsonString`. Listing is **not**
 * permission-filtered in this milestone: `command.list` advertises every
 * command's schema to any read caller because no `manage_users` command exists
 * yet (threat model T-20); this MUST be revisited to filter unauthorized
 * commands when Task 0002 adds `manage_users` descriptors.
 *
 * @param descriptor The descriptor to serialize.
 * @return One well-formed JSON object as text.
 */
std::string descriptor_to_json(const CommandDescriptor& descriptor);

/**
 * @brief Registers the built-in `command.list` command on @p registry.
 *
 * `command.list` is `read`, `sync`, non-mutating and takes no arguments. Its
 * handler is pure: it reads only @p registry's descriptor set (via @ref
 * CommandRegistry::list) and returns
 * `{"commands":[<descriptor_to_json>,...]}` in name-sorted order, touching no
 * I/O, service or storage. Being non-mutating it writes no audit record.
 *
 * Lifetime: the handler captures `&registry`, so the registry MUST outlive the
 * handler. That is guaranteed because the handler is stored inside the same
 * registry, which is non-copyable and non-movable; the handler can therefore
 * never outlive the object it references.
 *
 * @param registry The registry to add `command.list` to and whose descriptors
 * the handler lists; must outlive every call to the registered handler.
 */
void register_builtin_commands(CommandRegistry& registry);

}  // namespace url_shortener::commands
