/*
 * Copyright 2017-2018 the DCSS authors
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <cassert>
#include <iostream>

#include "shell.h"

namespace dcss {

static char sargmem[SHELL_MAX_ARGV][SHELL_MAX_ARG_LEN];
static char* sargv[SHELL_MAX_ARGV + 1];
static int sargc;

const char* shell_err_strings[] = {
    "No error",
    "too many args",
    "argument is too long",
    "double quote error",
    nullptr,
};

static const char* shell_error_str(enum shell_error err)
{
    return (shell_err_strings[err]);
}

static struct cmd_def** g_cmd_defs = nullptr;

static char* command_generator(const char* text, int state)
{
    static int list_index;
    static size_t len;
    const char* name;
    struct cmd_def* def;

    if (state == 0) {
        list_index = 0;
        len = strlen(text);
    }

    while (g_cmd_defs[list_index] != nullptr) {
        def = g_cmd_defs[list_index];
        name = def->name;

        list_index++;

        if (strncmp(name, text, len) == 0) {
            char* nptr = strdup(name);
            assert(nullptr != nptr);
            return nptr;
        }
    }

    /* If no names matched, then return NULL. */
    return nullptr;
}

static char** shell_completion(const char* text, int start, int /*end*/)
{
    char** matches;

    if (nullptr == g_cmd_defs) {
        return nullptr;
    }

    matches = nullptr;

    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    }

    return (matches);
}

bool Shell::class_initialized = false;

Shell::Shell()
{
    if (!Shell::class_initialized) {
        rl_attempted_completion_function = shell_completion;
        Shell::class_initialized = true;
    }

    defs = nullptr;
    handle = nullptr;
    handle2 = nullptr;
}

int Shell::do_cmd(struct cmd_def** definitions, int argc, char** argv)
{
    struct cmd_def *def, *tmp;
    size_t len;
    int ret;
    bool found;
    char* p;

    if (argc == 0) {
        return SHELL_CONT;
    }

    if ('!' == argv[0][0]) {
        pid_t pid;

        pid = fork();
        if (-1 == pid) {
            perror("fork");
            return SHELL_CONT;
        }
        if (0 == pid) {
            argv[0]++;

            ret = execvp(argv[0], argv);
            if (-1 == ret) {
                perror("execvp");
                exit(1);
            }
        } else {
        retry:
            ret = wait(nullptr);
            if (-1 == ret) {
                if (EINTR == errno) {
                    goto retry;
                } else {
                    perror("wait");
                    exit(1);
                }
            }
        }
        return SHELL_CONT;
    }
    if (nullptr != (p = index(argv[0], '='))) {
        *p++ = 0;
        return SHELL_CONT;
    }
    len = strlen(argv[0]);

    def = nullptr;
    found = false;
    for (int i = 0; definitions[i] != nullptr; i++) {
        tmp = definitions[i];
        if (strcmp(argv[0], tmp->name) == 0) {
            found = true;
            def = tmp;
            break;
        }
        if (strncmp(argv[0], tmp->name, len) == 0) {
            if (found) {
                std::cerr << "ambiguous command: " << argv[0] << '\n';
                return SHELL_CONT;
            }
            found = true;
            def = tmp;
        }
    }

    if (!found) {
        std::cerr << "cmd " << argv[0] << ": not found\n";
        return SHELL_CONT;
    }

    ret = def->func(this, argc, argv);
    return ret;
}

/** Parse a string into multiple commands.
 *
 * Everything is done static.
 *
 * Understands comments (sharp sign), double quotes, semicolon. ignore
 * whitspaces
 *
 * @param definitions the callback
 * @param str the input string
 * @param errp the error code
 *
 * @return 0 if OK, -1 on failure
 */
int Shell::parse(
    struct cmd_def** definitions,
    char* str,
    enum shell_error* errp)
{
    int pos, ret;
    bool comment = false;
    bool dblquote = false;
    bool skip_ws = true;
    char c;

    sargc = pos = 0;

    while (true) {
        c = *str;

        switch (c) {
        case '"':
            if (dblquote) {
                dblquote = false;
                if (0 == pos) {
                    skip_ws = false;
                    c = 0;
                    goto store_it;
                }
            } else {
                dblquote = true;
            }
            break;

        case '#':
            comment = true;
            break;

        case '\0':
        case ';':
        case '\n':

            if (c == ';' && dblquote) {
                skip_ws = false;
                goto store_it;
            }

            sargv[sargc] = &sargmem[sargc][0];

            if (!skip_ws) {
                sargc++;
            }

            sargv[sargc] = nullptr;

            if (dblquote) {
                if (errp != nullptr) {
                    *errp = SHELL_ERROR_DBL_QUOTE;
                }
                return -1;
            }

            if ((ret = do_cmd(definitions, sargc, sargv)) != SHELL_CONT) {
                return ret;
            }

            memset(sargmem, 0, sizeof(sargmem));

            sargc = pos = 0;
            comment = false;
            dblquote = false;
            skip_ws = true;

            if (c == '\0') {
                return 0;
            }

            break;

        case ' ':
        case '\t':

            if (comment) {
                break;
            }

            if (dblquote) {
                skip_ws = false;
                goto store_it;
            }

            if (!skip_ws) {
                if ((sargc + 1) < SHELL_MAX_ARGV) {
                    sargv[sargc] = &sargmem[sargc][0];
                    sargc++;
                    sargv[sargc] = nullptr;
                    pos = 0;
                } else {
                    if (errp != nullptr) {
                        *errp = SHELL_ERROR_TOO_MANY_ARGS;
                    }
                    return -1;
                }
                skip_ws = true;
            }
            break;

        default:

            if (comment) {
                break;
            }
            skip_ws = false;

        store_it:
            if ((pos + 1) < SHELL_MAX_ARG_LEN) {
                sargmem[sargc][pos++] = c;
                sargmem[sargc][pos] = 0;
            } else {
                if (errp != nullptr) {
                    *errp = SHELL_ERROR_ARG_TOO_LONG;
                }
                return -1;
            }
        }
        str++;
    }
}

void Shell::set_cmds(struct cmd_def** definitions)
{
    g_cmd_defs = this->defs = definitions;
}

void Shell::set_handle(void* hdl)
{
    this->handle = hdl;
}

void Shell::set_handle2(void* hdl)
{
    this->handle2 = hdl;
}

void* Shell::get_handle()
{
    return handle;
}

void* Shell::get_handle2()
{
    return handle2;
}

void Shell::set_prompt(const std::string& ps1)
{
    this->prompt = ps1;
}

void Shell::loop()
{
    using_history();

    while (true) {
        char* line = nullptr;

        if ((line = readline(prompt.c_str())) != nullptr) {
            enum shell_error shell_err = SHELL_ERROR_NONE;
            int ret;

            ret = parse(defs, line, &shell_err);
            if (ret == SHELL_EPARSE) {
                std::cerr << "parsing: " << shell_error_str(shell_err) << '\n';
            } else if (ret == SHELL_RETURN) {
                return;
            }
            if (strcmp(line, "") != 0) {
                add_history(line);
            }
            free(line); // NOLINT(hicpp-no-malloc)
        } else {
            std::cerr << "quit\n";
            return;
        }
    }
}

} // namespace dcss
