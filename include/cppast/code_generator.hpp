// Copyright (C) 2017 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef CPPAST_CODE_GENERATOR_HPP_INCLUDED
#define CPPAST_CODE_GENERATOR_HPP_INCLUDED

#include <cstring>

#include <type_safe/index.hpp>

#include <cppast/cpp_entity.hpp>
#include <cppast/cpp_entity_ref.hpp>

namespace cppast
{
    /// A simple string view implementation, like [std::string_view]().
    ///
    /// It "views" - stores a pointer to - some kind of string.
    class string_view
    {
    public:
        /// \effects Creates it viewing the [std::string]().
        string_view(const std::string& str) noexcept : str_(str.c_str()), length_(str.length())
        {
        }

        /// \effects Creates it viewing the C string `str`.
        string_view(const char* str) noexcept : str_(str), length_(std::strlen(str))
        {
        }

        /// \effects Creates it viewing the C string literal.
        template <std::size_t Size>
        constexpr string_view(const char (&str)[Size]) noexcept : str_(str), length_(Size - 1u)
        {
        }

        /// \returns The number of characters.
        constexpr std::size_t length() const noexcept
        {
            return length_;
        }

        /// \returns The C string.
        constexpr const char* c_str() const noexcept
        {
            return str_;
        }

        /// \returns The character at the given index.
        /// \requires `i < length()`.
        char operator[](type_safe::index_t i) const noexcept
        {
            return type_safe::at(str_, i);
        }

    private:
        const char* str_;
        std::size_t length_;
    };

    /// \exclude
    namespace detail
    {
        template <typename Tag>
        class semantic_string_view
        {
        public:
            template <typename T>
            explicit semantic_string_view(T&& obj,
                                          decltype(string_view(std::forward<T>(obj)), 0) = 0)
            : str_(std::forward<T>(obj))
            {
            }

            const string_view& str() const noexcept
            {
                return str_;
            }

        private:
            string_view str_;
        };
    } // namespace detail

    /// A special [cppast::string_view]() representing a C++ keyword token.
    using keyword = detail::semantic_string_view<struct keyword_tag>;

    /// A special [cppast::string_view]() representing a C++ identifier token.
    using identifier = detail::semantic_string_view<struct identifier_tag>;

    /// A special [cppast::string_view]() representing a C++ string or character literal token.
    using string_literal = detail::semantic_string_view<struct str_literal_tag>;

    /// A special [cppast::string_view]() representing a C++ integer literal token.
    using int_literal = detail::semantic_string_view<struct int_literal_tag>;

    /// A special [cppast::string_view]() representing a C++ floating point literal token.
    using float_literal = detail::semantic_string_view<struct float_literal_tag>;

    /// A special [cppast::string_view]() representing a C++ punctuation token like `.` or `(`.
    using punctuation = detail::semantic_string_view<struct punctuation_tag>;

    /// A special [cppast::string_view]() representing a C++ preprocessor token.
    using preprocessor_token = detail::semantic_string_view<struct preprocessor_tag>;

    /// A special [cppast::string_view]() representing a sequence of unknown C++ tokens.
    using token_seq = detail::semantic_string_view<struct token_seq_tag>;

    /// Tag type to represent an end-of-line character.
    const struct newl_t
    {
    } newl{};

    /// Tag type to represent a single space character.
    const struct whitespace_t
    {
    } whitespace{};

    /// Base class to control the code generation.
    ///
    /// Inherit from it to customize how a [cppast::cpp_entity]() is printed
    /// by [cppast::generate_code]().
    class code_generator
    {
    public:
        code_generator(const code_generator&) = delete;
        code_generator& operator=(const code_generator&) = delete;
        virtual ~code_generator() noexcept               = default;

        /// Options that control the synopsis.
        enum synopsis_options
        {
            exclude,     //< Exclude the entire entity.
            declaration, //< Only write declaration.
            definition,  //< Also write definition.
        };

        /// Sentinel type used to output a given entity.
        class output
        {
        public:
            /// \effects Creates it giving the generator, the entity
            /// and whether or not the entity is a container.
            /// It is a container if while this object lives
            /// any other `output` objects are created.
            ///
            /// It will call `on_container_begin()` or `on_leaf()`,
            /// respectively.
            explicit output(type_safe::object_ref<code_generator>   gen,
                            type_safe::object_ref<const cpp_entity> e, bool is_container)
            : gen_(gen), options_(is_container ? gen_->on_container_begin(*e) : gen_->on_leaf(*e))
            {
                if (is_container)
                    e_ = e;
            }

            /// \effects If the entity is a container
            /// and `on_container_begin()` returned `true`,
            /// calls `on_container_end()`,
            /// else does nothing.
            ~output() noexcept
            {
                if (*this && e_)
                    gen_->on_container_end(e_.value());
            }

            output(const output&) = delete;
            output& operator=(const output&) = delete;

            /// \returns Whether or not the `on_XXX` function returned something other than `exclude`.
            /// \notes If this returns `false`,
            /// the other functions have no effects.
            explicit operator bool() const noexcept
            {
                return options_ != exclude;
            }

            /// \returns Whether or not the definition should be generated as well.
            bool generate_definition() const noexcept
            {
                return options_ == definition;
            }

            /// \returns A reference to the generator.
            type_safe::object_ref<code_generator> generator() const noexcept
            {
                return gen_;
            }

            /// \effects Call `do_indent()` followed by `do_write_newline()` (if `print_newline` is `true`).
            void indent(bool print_newline = true) const noexcept
            {
                if (*this)
                {
                    gen_->do_indent();
                    if (print_newline)
                        gen_->do_write_newline();
                }
            }

            /// \effects Calls `do_unindent()`.
            void unindent() const noexcept
            {
                if (*this)
                    gen_->do_unindent();
            }

            /// \effects Calls `do_write_keyword()`.
            const output& operator<<(const keyword& k) const
            {
                if (*this)
                    gen_->do_write_keyword(k.str());
                return *this;
            }

            /// \effects Calls `do_write_identifier()`.
            const output& operator<<(const identifier& ident) const
            {
                if (*this)
                    gen_->do_write_identifier(ident.str());
                return *this;
            }

            /// \effects Calls `do_write_reference()`.
            template <typename T, class Predicate>
            const output& operator<<(const basic_cpp_entity_ref<T, Predicate>& ref) const
            {
                if (*this)
                    gen_->do_write_reference(ref.id(), ref.name());
                return *this;
            }

            /// \effects Calls `do_write_punctuation()`.
            const output& operator<<(const punctuation& punct) const
            {
                if (*this)
                    gen_->do_write_punctuation(punct.str());
                return *this;
            }

            /// \effects Calls `do_write_str_literal`.
            const output& operator<<(const string_literal& lit) const
            {
                if (*this)
                    gen_->do_write_str_literal(lit.str());
                return *this;
            }

            /// \effects Calls `do_write_int_literal()`.
            const output& operator<<(const int_literal& lit) const
            {
                if (*this)
                    gen_->do_write_int_literal(lit.str());
                return *this;
            }

            /// \effects Calls `do_write_float_literal()`.
            const output& operator<<(const float_literal& lit) const
            {
                if (*this)
                    gen_->do_write_float_literal(lit.str());
                return *this;
            }

            /// \effects Calls `do_write_preprocessor()`.
            const output& operator<<(const preprocessor_token& tok) const
            {
                if (*this)
                    gen_->do_write_preprocessor(tok.str());
                return *this;
            }

            /// \effects Calls `do_write_token_seq()`.
            const output& operator<<(const token_seq& seq) const
            {
                if (*this)
                    gen_->do_write_token_seq(seq.str());
                return *this;
            }

            /// \effects Calls `do_write_newline()`.
            const output& operator<<(newl_t) const
            {
                if (*this)
                    gen_->do_write_newline();
                return *this;
            }

            /// \effects Calls `do_write_whitespace()`.
            const output& operator<<(whitespace_t) const
            {
                if (*this)
                    gen_->do_write_whitespace();
                return *this;
            }

        private:
            type_safe::object_ref<code_generator>     gen_;
            type_safe::optional_ref<const cpp_entity> e_;
            synopsis_options                          options_;
        };

    protected:
        code_generator() noexcept = default;

    private:
        /// \effects Will be invoked before code of a container entity is generated.
        /// The base class version has no effect.
        /// \returns The synopsis options for that entity,
        /// the base class version always returns `definition`.
        virtual synopsis_options on_container_begin(const cpp_entity& e)
        {
            (void)e;
            return definition;
        }

        /// \effects Will be invoked after all code of a container entity has been generated.
        /// The base class version has no effect.
        virtual void on_container_end(const cpp_entity& e)
        {
            (void)e;
        }

        /// \effects Will be invoked before code of a non-container entity is generated.
        /// The base class version has no effect.
        /// \returns The synopsis options for that entity,
        /// the base class version always returns `definition`.
        virtual synopsis_options on_leaf(const cpp_entity& e)
        {
            (void)e;
            return definition;
        }

        /// \effects Will be invoked when the indentation level should be increased by one.
        /// The level change must be applied on the next call to `do_write_newline()`.
        virtual void do_indent() = 0;

        /// \effects Will be invoked when the indentation level should be decreased by one.
        /// The level change must be applied immediately if nothing else has been written on the current line.
        virtual void do_unindent() = 0;

        /// \effects Writes the given token sequence.
        virtual void do_write_token_seq(string_view tokens) = 0;

        /// \effects Writes the specified special token.
        /// The base class version simply forwards to `do_write_token_seq()`.
        /// \notes This is useful for syntax highlighting, for example.
        /// \group write
        virtual void do_write_keyword(string_view keyword)
        {
            do_write_token_seq(keyword);
        }
        /// \group write
        virtual void do_write_identifier(string_view identifier)
        {
            do_write_token_seq(identifier);
        }
        /// \group write
        virtual void do_write_reference(type_safe::array_ref<const cpp_entity_id> id,
                                        string_view                               name)
        {
            (void)id;
            do_write_token_seq(name);
        }
        /// \group write
        virtual void do_write_punctuation(string_view punct)
        {
            do_write_token_seq(punct);
        }
        /// \group write
        virtual void do_write_str_literal(string_view str)
        {
            do_write_token_seq(str);
        }
        /// \group write
        virtual void do_write_int_literal(string_view str)
        {
            do_write_token_seq(str);
        }
        /// \group write
        virtual void do_write_float_literal(string_view str)
        {
            do_write_token_seq(str);
        }
        /// \group write
        virtual void do_write_preprocessor(string_view punct)
        {
            do_write_token_seq(punct);
        }

        /// \effects Writes a newline.
        /// It is guaranteed that this is the only way a newline will be printed.
        /// The base class forwards to `do_write_token_seq()`.
        virtual void do_write_newline()
        {
            do_write_token_seq("\n");
        }
        /// \effects Writes a whitespace character.
        /// It will be invoked only where a whitespace is truly needed,
        /// like between two keywords.
        /// The base class forwards to `do_write_token_seq()`.
        virtual void do_write_whitespace()
        {
            do_write_token_seq(" ");
        }
    };

    /// Generates code for the given entity.
    ///
    /// How the code is generated is customized by the generator.
    /// The implementation will write whitespace only where necessary,
    /// but a newline after each entity.
    /// This allows customization of formatting.
    void generate_code(code_generator& generator, const cpp_entity& e);

    /// \exclude
    class cpp_template_argument;

    /// \exclude
    namespace detail
    {
        void write_template_arguments(code_generator::output&                           output,
                                      type_safe::array_ref<const cpp_template_argument> arguments);
    } // namespace detail
} // namespace cppast

#endif // CPPAST_CODE_GENERATOR_HPP_INCLUDED
