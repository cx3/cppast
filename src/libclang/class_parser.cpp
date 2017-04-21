// Copyright (C) 2017 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#include <cppast/cpp_class.hpp>
#include <clang-c/Index.h>

#include "libclang_visitor.hpp"
#include "parse_functions.hpp"

using namespace cppast;

namespace
{
    cpp_class_kind parse_class_kind(const CXCursor& cur)
    {
        auto kind = clang_getTemplateCursorKind(cur);
        if (kind == CXCursor_NoDeclFound)
            kind = clang_getCursorKind(cur);
        switch (kind)
        {
        case CXCursor_ClassDecl:
            return cpp_class_kind::class_t;
        case CXCursor_StructDecl:
            return cpp_class_kind::struct_t;
        case CXCursor_UnionDecl:
            return cpp_class_kind::union_t;
        default:
            break;
        }
        DEBUG_UNREACHABLE(detail::assert_handler{});
        return cpp_class_kind::class_t;
    }

    cpp_class::builder make_class_builder(const CXCursor& cur)
    {
        auto kind = parse_class_kind(cur);
        auto name = detail::get_cursor_name(cur);
        return cpp_class::builder(name.c_str(), kind);
    }

    cpp_access_specifier_kind convert_access(const CXCursor& cur)
    {
        switch (clang_getCXXAccessSpecifier(cur))
        {
        case CX_CXXInvalidAccessSpecifier:
            break;

        case CX_CXXPublic:
            return cpp_public;
        case CX_CXXProtected:
            return cpp_protected;
        case CX_CXXPrivate:
            return cpp_private;
        }

        DEBUG_UNREACHABLE(detail::assert_handler{});
        return cpp_public;
    }

    void add_access_specifier(cpp_class::builder& builder, const CXCursor& cur)
    {
        DEBUG_ASSERT(cur.kind == CXCursor_CXXAccessSpecifier, detail::assert_handler{});
        builder.access_specifier(convert_access(cur));
    }

    void add_base_class(cpp_class::builder& builder, const detail::parse_context& context,
                        const CXCursor& cur, const CXCursor& class_cur)
    {
        DEBUG_ASSERT(cur.kind == CXCursor_CXXBaseSpecifier, detail::assert_handler{});
        auto access     = convert_access(cur);
        auto is_virtual = clang_isVirtualBase(cur) != 0u;

        detail::tokenizer    tokenizer(context.tu, context.file, cur);
        detail::token_stream stream(tokenizer, cur);

        // [<attribute>] [virtual] [<access>] <name>
        // can't use spelling to get the name
        detail::skip_attribute(stream);
        if (is_virtual)
            detail::skip(stream, "virtual");
        detail::skip_if(stream, to_string(access));

        auto name = detail::to_string(stream, stream.end());

        auto type = detail::parse_type(context, class_cur, clang_getCursorType(cur));
        builder.base_class(std::move(name), std::move(type), access, is_virtual);
    }
}

std::unique_ptr<cpp_entity> detail::parse_cpp_class(const detail::parse_context& context,
                                                    const CXCursor& cur, const CXCursor& parent_cur)
{
    auto is_templated = (clang_getTemplateCursorKind(cur) != CXCursor_NoDeclFound
                         || !clang_Cursor_isNull(clang_getSpecializedCursorTemplate(cur)));
#if CPPAST_CINDEX_HAS_FRIEND
    auto is_friend = clang_getCursorKind(parent_cur) == CXCursor_FriendDecl;
#else
    (void)parent_cur;
    auto is_friend = false;
#endif

    auto                                builder = make_class_builder(cur);
    type_safe::optional<cpp_entity_ref> semantic_parent;
    if (!is_friend)
    {
        if (!clang_equalCursors(clang_getCursorSemanticParent(cur),
                                clang_getCursorLexicalParent(cur)))
        {
            // out-of-line definition
            detail::tokenizer    tokenizer(context.tu, context.file, cur);
            detail::token_stream stream(tokenizer, cur);

            std::string name = detail::get_cursor_name(cur).c_str();
            auto        pos  = name.find('<');
            if (pos != std::string::npos)
                name.erase(pos, std::string::npos);

            std::string scope;
            while (!detail::skip_if(stream, name.c_str()))
            {
                if (!detail::append_scope(stream, scope))
                    stream.bump();
            }
            if (!scope.empty())
                semantic_parent =
                    cpp_entity_ref(detail::get_entity_id(clang_getCursorSemanticParent(cur)),
                                   std::move(scope));
        }

        context.comments.match(builder.get(), cur);
        detail::visit_children(cur, [&](const CXCursor& child) {
            auto kind = clang_getCursorKind(child);
            if (kind == CXCursor_CXXAccessSpecifier)
                add_access_specifier(builder, child);
            else if (kind == CXCursor_CXXBaseSpecifier)
                add_base_class(builder, context, child, cur);
            else if (kind == CXCursor_CXXFinalAttr)
                builder.is_final();
            else if (
                kind == CXCursor_TemplateTypeParameter || kind == CXCursor_NonTypeTemplateParameter
                || kind == CXCursor_TemplateTemplateParameter || kind == CXCursor_ParmDecl
                || clang_isExpression(kind) || clang_isReference(kind)
                || kind
                       == CXCursor_UnexposedAttr) // I have no idea what this is, but happens on Windows
                // other children due to templates and stuff
                return;
            else if (auto entity = parse_entity(context, child))
                builder.add_child(std::move(entity));
        });
    }

    if (!is_friend && clang_isCursorDefinition(cur))
        return is_templated ?
                   builder.finish(std::move(semantic_parent)) :
                   builder.finish(*context.idx, get_entity_id(cur), std::move(semantic_parent));
    else
        return is_templated ? builder.finish_declaration(detail::get_entity_id(cur)) :
                              builder.finish_declaration(*context.idx, get_entity_id(cur));
}
