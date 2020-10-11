#ifndef _FT_H
#define _FT_H

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

// TODO This is only available under certain compilers
#define FT_COMPUTED_GOTO 1

// Logs for debugging, if needed


// Trace all virtual machine access
#define FT_VM (1 << 1)
// Trace all evaluation code
#define FT_EVAL (1 << 2)
// Trace various runtime things
#define FT_RT (1 << 3)
// Trace compilation emission
#define FT_CC (1 << 4)

// #define FT_LOG_TAGS (FT_VM + FT_RT + FT_CC)
#define FT_LOG_TAGS (FT_VM)

#if FT_LOG_TAGS
# define FT_LOG(tag, exp) do { if(((FT_LOG_TAGS) & tag)) { std::cout << exp << std::endl; } } while(0);
#else
# define FT_LOG(tag, exp)
#endif 

#define FT_ASSERT(x) assert(x)

/**
 * Check for an error and return if there was one
 */
#define FT_CHECK(e) do { ft::Error err = e; if(err != E_OK) { return err; }} while(0)

#define FT_CHECKF(e, ...) do { ft::Error err = e; if(err != E_OK) { return errorf(err, __VA_ARGS__); }} while(0)

/**
 * Size of scratch buffer to use for things like formatting strings and reading input
 */
#ifndef FT_SCRATCH_SIZE
# define FT_SCRATCH_SIZE 512
#endif

/**
 * Number of pointers to share between C++ and Forth
 */
#ifndef FT_SHARED_SIZE 
# define FT_SHARED_SIZE 8
#endif

namespace ft {

inline size_t align(int boundary, size_t value) {
  return (size_t)((value + (boundary - 1)) & -boundary);
}

struct State;

/**
 * A word (as in system word) sized integer.
 */
struct Cell {
  Cell(): bits(0) {}
  Cell(ptrdiff_t bits_): bits(bits_) {}

  ptrdiff_t bits;

  template <class T> T* as() const {
    return (T*) bits;
  }

  template <class T> void set(T* ptr) {
    bits = (ptrdiff_t) ptr;
  }

  ptrdiff_t operator*() const {
    return bits;
  }
};

/** 
 * Error codes, returned directly by functions
 */
enum Error {
  E_OK,
  E_STACK_UNDERFLOW,
  E_STACK_OVERFLOW,
  E_OUT_OF_RANGE,
  E_OUT_OF_MEMORY,
  /** Encountered something that was too large for scratch space, such as a very long word name */
  E_OUT_OF_SCRATCH,
  /** For defining words -- requests that a word is available in scratch space. */
  E_WANT_WORD,
  /** Word not found */
  E_WORD_NOT_FOUND,
  E_DIVIDE_BY_ZERO,
  /** Unknown opcode encountered in VM -- most likely something bad was written by a Forth word */
  E_INVALID_OPCODE,
  E_INVALID_ADDRESS,
  /** Attempt to invoke a compile only word in interpreter mode */
  E_COMPILE_ONLY,
  E_EXPECTED_FORTH_WORD,
  E_EXPECTED_C_WORD,
};

inline const char* error_description(const Error e) {
  switch(e) {
    case E_OK: return "ok";
    case E_STACK_UNDERFLOW: return "stack underflow";
    case E_STACK_OVERFLOW: return "stack overflow";
    case E_OUT_OF_MEMORY: return "out of memory";
    case E_WANT_WORD: return "wanted a word";
    case E_WORD_NOT_FOUND: return "word not found";
    case E_DIVIDE_BY_ZERO: return "divide by zero";
    case E_INVALID_OPCODE: return "invalid opcode";
    case E_INVALID_ADDRESS: return "invalid address";
    case E_COMPILE_ONLY: return "invoked compile only word from interpreter";
    case E_EXPECTED_FORTH_WORD: return "expected forth word";
    case E_EXPECTED_C_WORD: return "expected c word";
    default: return "unknown";
  }
}

/** A generic stack memory structure, since we have several */
struct Stack {
  Stack(): data(0), i(0), size(0) {}
  ptrdiff_t* data;
  size_t i;
  size_t size;

  void zero() { memset(data, 0, size * sizeof(ptrdiff_t)); }

  template <class T>
  Error get(ptrdiff_t idx, T& out) {
    if(idx >= i) {
      return E_OUT_OF_RANGE;
    }

    out = (T) data[idx];

    return E_OK;
  }


  Error push(ptrdiff_t w) {
    if(i > size) {
      // TODO return which stack this happened on
      return E_OUT_OF_MEMORY;
    }

    data[i++] = w;

    return E_OK;
  }
};

/**
 * StateConfig -- a struct used to initialize State and point it at 
 * whatever memory you've allocated for it.
 */
struct StateConfig {
  StateConfig() {}
  ~StateConfig() {}

  Cell* stack;
  size_t stack_size;

  char* memory;
  size_t memory_size;

  Cell* shared;
  size_t shared_size;

  Stack locals;
  Stack cwords;
};


/** 
 * A convenience method for struct StateConfig with statically allocated memory
 */
template <size_t stack_size_num = 1024, size_t shared_size_num = 8, size_t locals_size_num = 256, size_t cwords_size_num = 128, size_t memory_size_num = 1024 * 1024>
struct StaticStateConfig : StateConfig {
  StaticStateConfig() {
    stack = (Cell*) stack_store;
    stack_size = stack_size_num;

    memory = (char*) memory_store;
    memory_size = memory_size_num;

    locals.data = locals_store;
    locals.size = locals_size_num;

    cwords.data = cwords_store;
    cwords.size = cwords_size_num;

    shared = (Cell*) shared_store;
    shared_size = shared_size_num;
  }

  Cell* stack_store[stack_size_num];
  char* memory_store[memory_size_num];
  ptrdiff_t locals_store[locals_size_num];
  ptrdiff_t cwords_store[cwords_size_num];
  Cell* shared_store[shared_size_num];
};

/** 
 * An entry in the Forth dictionary
 */
struct DictEntry {
  enum Flags {
    FLAG_NONE = 0,
    FLAG_IMMEDIATE = 1 << 1,
    FLAG_CWORD = 1 << 2,
    FLAG_HIDDEN = 1 << 3,
    FLAG_COMPILE_ONLY = 1 << 4,
  };

  /**
   * The previous dictionary entry (if any)
   */
  DictEntry* previous;

  /**
   * Flags on the word
   */
  size_t flags;

  size_t name_length;

  char name[1];
  
  // The actual data in the dictionary comes afterwards
  template <class T> T* data() const {
    return (T*) (((size_t) this) + sizeof(DictEntry) + align(sizeof(ptrdiff_t), name_length + 1));
  }
};

struct State;

/**
 * c_word_t is a C++ function that can do arbitrary things with a State, used to define Forth words
 * backed by C++ functions.
 */
typedef Error (*c_word_t)(State&);

/*a*
 * Reserved C++ and Forth variables. 
 * Start at S_USER_SHARED to define your own.
 */
enum {
  /** Latest dictionary entry */
  S_LATEST,
  /** Current location in memory */
  S_HERE, 
  /** Input mode */
  S_WORD_AVAILABLE,
  /** Compiling */
  S_COMPILING,
  /** Local count, count of locals emitted in { */
  S_LOCAL_COUNT,
  /** For compilation -- a root node to reset LATEST to after compilation completes */
  S_DICT_ROOT,
  S_USER_SHARED,
};

enum { INPUT_INTERPRET, INPUT_PASS_WORD };

enum Token { TK_NUMBER, TK_WORD, TK_END, };

enum Opcode {
  /** Null -- should not be encountered */
  OP_UNKNOWN = 0,
  /** Push an immediate value */
  OP_PUSH_IMMEDIATE = 1,
  /** Call another Forth word */
  OP_CALL_FORTH = 2,
  /** Call out to a C++ defined word. Must be followed by a C++ function address */
  OP_CALL_C = 3,
  /** Jump to next address if top of stack is zero */
  OP_JUMP_IF_ZERO = 4,
  /** Jump to the next address */
  OP_JUMP = 5,
  /** Jump to the next address, and ignore for the purposes of decompiling */
  // TODO: It would be nicer to have this as a second operand
  // for jump
  OP_JUMP_IGNORED = 6,
  /** Push a local value onto the data stack */
  OP_LOCAL_PUSH = 7,
  /** Pop a value off of the data stack and push it onto the local stack */
  OP_LOCAL_SET = 8,
  /** Exit current word */
  OP_EXIT = 9,
};

/**
 * An instance of Forth. Self-contained and re-entrant
 */
struct State {
  State(const StateConfig& cfg):
    stack(cfg.stack),
    stack_size(cfg.stack_size),
    si(0),
    memory(cfg.memory),
    memory_i(0),
    memory_size(cfg.memory_size),
    shared(cfg.shared),
    shared_size(cfg.shared_size),
    locals(cfg.locals),
    cwords(cfg.cwords),
    scratch_i(0) {
      // Zero out memory
      memset(stack, 0, stack_size * sizeof(Cell));
      memset(memory, 0, memory_size);
      memset(scratch, 0, FT_SCRATCH_SIZE);
      memset(shared, 0, shared_size * sizeof(Cell));
      cwords.zero();
      locals.zero();

      /***** BUILTIN WORDS */

      /***** ARITHMETIC / COMPARISON */

      cwords.push(0);

      defw("+", [](State& s) {
        Cell a, b;
        FT_CHECK(s.pop(a));
        FT_CHECK(s.pop(b));
        return s.push(b.bits + a.bits);
      });

      defw("*", [](State& s) {
        Cell a, b;
        FT_CHECK(s.pop(a));
        FT_CHECK(s.pop(b));
        return s.push(a.bits * b.bits);
      });

      defw("-", [](State& s) {
        Cell a, b;
        FT_CHECK(s.pop(a));
        FT_CHECK(s.pop(b));
        return s.push(b.bits - a.bits);
      });

      defw(">", [](State& s) {
        Cell a, b;
        FT_CHECK(s.pop(a));
        FT_CHECK(s.pop(b));
        return s.push(b.bits > a.bits ? -1 : 0);
      });

      /***** I/O */
      defw(".", [](State& s) {
        Cell x;
        FT_CHECK(s.pop(x));
        printf("%ld\n", x.bits);
        return E_OK;
      });

      /***** META / SYSTEM WORDS */

      defw(":", [](State& s) {
        // Wait for word in scratch
        if(*s.shared[S_WORD_AVAILABLE] == 0) {
          return E_WANT_WORD;
        }

        s.shared[S_WORD_AVAILABLE] = 0;
        s.shared[S_COMPILING] = 1;

        DictEntry* d = 0;
        Error e = s.create(s.scratch, d);
        if(e != E_OK) return e;
        s.shared[S_DICT_ROOT] = s.shared[S_LATEST];
        return E_OK;
      });

      defw(";", [](State& s) {
        FT_CHECK(s.dict_put(OP_EXIT));
        s.shared[S_COMPILING] = 0;
        s.shared[S_LATEST] = s.shared[S_DICT_ROOT];
        return E_OK;
      }, DictEntry::FLAG_IMMEDIATE + DictEntry::FLAG_COMPILE_ONLY);
      
      // Marks a word to be immediately executed, even when
      // in compiler mode
      defw("immediate", [](State& s) {
        DictEntry *d = s.shared[S_LATEST].as<DictEntry>();
        if((d->flags & DictEntry::FLAG_IMMEDIATE) == 0) {
          d->flags += DictEntry::FLAG_IMMEDIATE;
        }
        return E_OK;
      });

      // Marks a word as compile-only
      defw("compile-only", [](State& s) {
        DictEntry* d = s.shared[S_LATEST].as<DictEntry>();
        if((d-> flags & DictEntry::FLAG_COMPILE_ONLY) == 0) {
          d->flags += DictEntry::FLAG_COMPILE_ONLY;
        }
        return E_OK;
      });

      defw(",", [](State& s) {
        Cell c;
        FT_CHECK(s.pop(c));
        FT_CHECK(s.dict_put(c));
        // Write something to here and bump it 
        return E_OK;
      });

      defw("{", [](State& s) {
        // return want word until } is encountered, then stop wanting word
        if(*s.shared[S_WORD_AVAILABLE] == 0) {
          return E_WANT_WORD;
        }

        // If we found }, we're done
        // Emit code to set locals and mark local definitions as hidden
        if(strcmp(s.scratch, "}") == 0) {
          for(size_t i = 0; i != *s.shared[S_LOCAL_COUNT]; i++) {
            FT_CHECK(s.dict_put(OP_LOCAL_SET));
          }

          // TODO: Mark locals as hidden

          // Reset shared counters
          s.shared[S_LOCAL_COUNT].bits = 0;
          s.shared[S_WORD_AVAILABLE].bits = 0;

          // Then quit
          return E_OK;

        }

        // We're about to define a local word, so emit a jump past the dictionary entry to the rest
        // of this word's code
        FT_CHECK(s.dict_put(OP_JUMP_IGNORED));

        // Emit a nonsense address here to overwrite in one second
        // It would actually be possible to calculate this in advance
        // but kind of annoying

        ptrdiff_t* jmpaddr = (ptrdiff_t*)&s.memory[s.memory_i];
        FT_CHECK(s.dict_put(-1));

        // Create a new dictionary entry
        DictEntry *d = 0;
        FT_CHECK(s.create(s.scratch, d));

        d->flags = DictEntry::FLAG_COMPILE_ONLY + DictEntry::FLAG_IMMEDIATE;

        // Emit code to emit code to push local when this is called (SUCH WOW VERY META)
        FT_CHECK(s.dict_put(OP_PUSH_IMMEDIATE));
        FT_CHECK(s.dict_put(OP_LOCAL_PUSH));
        FT_CHECK(s.dict_put_cword(","));
        FT_CHECK(s.dict_put(OP_PUSH_IMMEDIATE));
        FT_CHECK(s.dict_put(*s.shared[S_LOCAL_COUNT]));
        FT_CHECK(s.dict_put_cword(","));
        FT_CHECK(s.dict_put(OP_EXIT));

        // Increment local count
        s.shared[S_LOCAL_COUNT] = (*s.shared[S_LOCAL_COUNT] + 1);

        (*jmpaddr) = (ptrdiff_t) s.real_to_raddr((ptrdiff_t*) &s.memory[s.memory_i]);

        // Then just continue
        s.shared[S_WORD_AVAILABLE].bits = 0;
        return E_WANT_WORD;
      }, DictEntry::FLAG_IMMEDIATE + DictEntry::FLAG_COMPILE_ONLY);

      /***** MEMORY MANIPULATION */

      defw("!", [](State& s) {
        // Store data at address
        Cell addrcell, data;
        FT_CHECK(s.pop(addrcell));
        FT_CHECK(s.pop(data));

        // TODO(raddr): writing directly to memory address
        ptrdiff_t *real, *raddr = addrcell.as<ptrdiff_t>();
        FT_CHECK(s.raddr_valid(raddr));

        real = s.raddr_to_real(raddr);

        (*real) = data.bits;

        return E_OK;
      });

      defw("here", [](State& s) {
        s.push(s.memory_i);
        return E_OK;
      });

      defw("WORD", [](State& s) {
        s.push(sizeof(ptrdiff_t));
        return E_OK;
      });

      defw("@", [](State& s) {
        Cell addrcell;
        FT_CHECK(s.pop(addrcell));

        ptrdiff_t* raddr = addrcell.as<ptrdiff_t>();
        FT_CHECK(s.raddr_valid(raddr));
        ptrdiff_t* real = s.raddr_to_real(raddr);

        return s.push(*real);
      });

      /***** STACK MANIPULATION WORDS */

      defw("dup", [](State& s) {
        Cell c;
        FT_CHECK(s.pick(0, c));
        return s.push(c);
      });

      defw("drop", [](State& s) {
        return s.drop(1);
      });

      defw("swap", [](State& s) {
        Cell a, b;
        FT_CHECK(s.pop(a));
        FT_CHECK(s.pop(b));
        FT_CHECK(s.push(a));
        return s.push(b);
      });

      defw("\'", [](State& s) {
        if(*s.shared[S_WORD_AVAILABLE] == 0) {
          return E_WANT_WORD;
        }

        s.shared[S_WORD_AVAILABLE] = 0;
        DictEntry* d = s.lookup(s.scratch);
        if(!d) return E_WORD_NOT_FOUND;

        if((d->flags & DictEntry::FLAG_CWORD) != 0) {
          // TODO: Give out cword addr
          return E_EXPECTED_FORTH_WORD;
        }

        return s.push((ptrdiff_t) s.real_to_raddr(d->data<ptrdiff_t>()));

        return E_OK;
      });

      // Print the VM code of a forth word. Assumes
      // it is given an execution token, will read
      // from addr to OP_EXIT
      defw("decompile", [](State& s) {
        Cell addrcell;
        // TODO: Check validity of address
        FT_CHECK(s.pop(addrcell));
        // TODO(raddr): Reading Forth memory directly
        ptrdiff_t* code = addrcell.as<ptrdiff_t>();
        size_t ip = 0;
        bool loop = true;
        while(loop) {
          ptrdiff_t opaddr = (ptrdiff_t) &code[ip];
          ptrdiff_t op = code[ip++];

          switch(op) {
            case OP_PUSH_IMMEDIATE: {
              printf("OP_PUSH_IMMEDIATE @ %ld (%ld)\n", opaddr, code[ip++]);
              break;
            }
            case OP_CALL_FORTH: {
              printf("OP_CALL_FORTH @ %ld (%ld)\n", opaddr, code[ip++]);
              break;
            }
            case OP_CALL_C: {
              printf("OP_CALL_C @ %ld (%ld)\n", opaddr, code[ip++]);
              break;
            }
            case OP_JUMP_IF_ZERO: {
              printf("OP_JUMP_IF_ZERO @ %ld (%ld)\n", opaddr, code[ip++]);
              break;
            }
            case OP_JUMP: {
              printf("OP_JUMP @ %ld (%ld)\n", opaddr, code[ip++]);
              break;
            }
            case OP_JUMP_IGNORED: {
              printf("OP_JUMP_IGNORED @ %ld (%ld)\n", opaddr, code[ip++]);
              code = (ptrdiff_t*) code[ip-1];
              ip = 0;
              break;
            }
            case OP_EXIT: {
              printf("OP_EXIT @ %ld\n", opaddr);
              loop = false;
              break;
            }
            case OP_LOCAL_PUSH: {
              printf("OP_LOCAL_PUSH @ %ld (%ld)\n", opaddr, code[ip++]);
              break;
            }
            case OP_LOCAL_SET: {
              printf("OP_LOCAL_SET\n");
              break;
            }
            case OP_UNKNOWN: default: {
              printf("E_INVALID_OPCODE @ %ld %ld\n", opaddr, op);
              loop = false;
              break;
            }
          }
        }
        return E_OK;
      });
    }
  ~State() {}

  /**
   * The data stack
   */
  Cell *stack;
  size_t stack_size, si;

  /**
   * Program memory
   */
  char *memory;
  size_t memory_i, memory_size;

  /**
   * Scratch buffer, for doing things with strings
   */
  char scratch[FT_SCRATCH_SIZE];
  size_t scratch_i;

  Cell* shared;
  size_t shared_size;

  /** Locals stack -- stores local variables during function execution */
  Stack locals;
  
  /** 
   * cwords stack -- stores C++ function addresses. Referencing them indirectly allows us to check
   * that we're jumping to a valid function before calling
   */
  Stack cwords;

  /***** STACK INTERACTION PRIMITIVES */

  Error push(Cell v) {
    stack[si++] = v;
    return E_OK;    
  };
  
  /**
   * Pop a value into v
   */
  Error pop(Cell& v) {
    if(si == 0) {
      return E_STACK_UNDERFLOW;
    }

    v = stack[si-1];
    si--;
    return E_OK;
  }

  /**
   * Drop N values from the stack
   */
  Error drop(size_t n = 1) {
    if(si < n) {
      return E_STACK_UNDERFLOW;
    }
    si -= n;
    return E_OK;
  }

  /**
   * Pick ith value off the stack (0 is top, 1 is one from the top etc)
   */
  Error pick(ptrdiff_t i, Cell& c) {
    if(i >= si) {
      return E_STACK_UNDERFLOW;
    }
    c = stack[si-i-1];
    return E_OK;
  }

  /***** MEMORY INTERACTION */

  /** Given a cword virtual address, find the actual function address */
  Error cword_get(ptrdiff_t raddr, c_word_t& cw) {
    // cword virtual addresses should always be odd this allows us to distinguish them from forth
    // word addresses
    if((raddr % 2) == 0) {
      return E_INVALID_OPCODE;
    }
    ptrdiff_t actual_idx = (raddr + 1) / 2;
    return cwords.get(actual_idx, cw);
  }

  /***** SCRATCH INTERACTION */

  Error scratch_put(char c) {
    if(scratch_i == FT_SCRATCH_SIZE) {
      return E_OUT_OF_SCRATCH;
    }
    scratch[scratch_i++] = c;
    return E_OK;
  }

  /**
   * Return (or propagate) an error message,
   * by appending a newline and writing to scratch spae
   */
  Error errorf_append(Error e, const char* fmt, ...) {
    if(scratch_i < FT_SCRATCH_SIZE - 1) {
      va_list va;
      va_start(va, fmt);
      scratch[scratch_i++] = '\n';
      vsnprintf(&scratch[scratch_i], FT_SCRATCH_SIZE - scratch_i, fmt, va);
      va_end(va);
    }
    return e;
  }

  Error errorf(Error e, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vsnprintf(scratch, FT_SCRATCH_SIZE, fmt, va);
    va_end(va);
    return e;
  }

  /***** DICTIONARY PRIMITIVES */

  /** Check whether a relative address if valid */
  Error raddr_valid(ptrdiff_t* addr) const {
    ptrdiff_t a = (ptrdiff_t) addr;
    if(a < 0 || a > memory_i) {
      return E_INVALID_ADDRESS;
    }
    return E_OK;
  }

  /** Convert a real pointer to a valid address */
  ptrdiff_t real_to_raddr(ptrdiff_t* real) const {
    return (ptrdiff_t)real - (ptrdiff_t)memory; 
  }

  /** Convert a relative address to a real pointer */
  ptrdiff_t* raddr_to_real(ptrdiff_t* ptr) const {
    return (ptrdiff_t*) &memory[(ptrdiff_t)ptr];
  }


  /**
   * Allocate some memory for general purpose use
   */
  Error allot(size_t req, char*& addr) {
    if(memory_i + req > memory_size) {
      return E_OUT_OF_MEMORY;
    }

    // TODO(raddr) giving a forth memory address directly
    addr = &memory[memory_i];

    memory_i += req;

    return E_OK;
  }

  /** Add a forth word */
  Error create(const char* name, DictEntry*& d) {
    size_t name_length = strlen(name);
    size_t size = sizeof(DictEntry) + align(sizeof(ptrdiff_t), name_length + 1);
    char* dict_addr = 0;

    FT_CHECK(allot(size, dict_addr));

    d = (DictEntry*) dict_addr;

    d->previous = shared[S_LATEST].as<DictEntry>();
    d->name_length = name_length;
    strncpy(d->name, name, name_length);

    FT_LOG(FT_RT, "create word " << name);

    FT_ASSERT(d->previous == shared[S_LATEST].as<DictEntry>());
    FT_ASSERT(d->name_length == name_length);
    FT_ASSERT(strcmp(d->name, name) == 0);

    shared[S_LATEST].set(d);

    return E_OK;
  }

  /**
   * Add a Forth word backed by a C++ function
   */
  Error defw(const char* name, c_word_t fnaddr, ptrdiff_t flags = 0) {
    DictEntry* d = 0;
    FT_CHECK(create(name, d));

    d->flags = DictEntry::FLAG_CWORD + flags;

    // Save the index of the cword in the cwords array
    size_t cword_idx = cwords.i == 0 ? 0 : (cwords.i * 2) - 1;
    FT_CHECK(cwords.push((size_t) fnaddr));

    FT_CHECK(dict_put(cword_idx));

    // TODO: It's possible for Forth code to overwrite this and cause us to call an invalid value
    // which would make ft.h crash. One possibility would be registering all C functions in an array
    // and only calling known indexes in that array. That way, even corrupted forth code could not
    // segfault, only call nonsensical C functions

    FT_ASSERT(*d->data<size_t>() == cword_idx);
    FT_ASSERT(d->flags & DictEntry::FLAG_CWORD);

    return E_OK;
  }

  Error require_cells(size_t cells) {
    if((memory_i + (sizeof(Cell) * cells)) > memory_size) {
      return E_OUT_OF_MEMORY;
    }
    return E_OK;
  }

  /** Push a cell into memory, comma in Forth */
  Error dict_put(Cell cell) {
    char* addr;
    FT_CHECK(allot(sizeof(ptrdiff_t), addr));

    FT_LOG(FT_CC, "emit " << cell.bits << " @ " << ((ptrdiff_t) &memory[memory_i-sizeof(ptrdiff_t)]) << " (relative) " << memory_i-sizeof(ptrdiff_t));

    (*((ptrdiff_t*)addr)) = cell.bits;

    return E_OK;
  }

  /** Push a c word invocation into memory */
  Error dict_put_cword(const char* word) {
    DictEntry* d = lookup(word);

    if(!d) return E_WORD_NOT_FOUND;

    if((d->flags & DictEntry::FLAG_CWORD) == 0) {
      // TODO FIX THIS
      return E_EXPECTED_C_WORD;
    }
    
    FT_CHECK(dict_put(OP_CALL_C));
    return dict_put(*d->data<ptrdiff_t>());
  }

  /**
   * Lookup a word in the dictionary
   */
  DictEntry* lookup(const char* name) const {
    DictEntry* e = shared[S_LATEST].as<DictEntry>();

    while(e) {
      // TODO: Skip HIDDEN
      if(strcmp(e->name, name) == 0) {
        return e;
      }
      e = e->previous;
    }

    return 0;
  }

  /***** MAIN INTERPRETER */

  const char* input;
  size_t input_i;
  size_t input_size;
  ptrdiff_t token_number;

  Error next_token(Token& tk) {
    char c;
    while(input_i < input_size) {
      char c = input[input_i++];
      if((c == '-' && input_i < input_size && isdigit(input[input_i])) || isdigit(c)) {
        // Number
        bool negative = false;
        if(c == '-') {
          c = input[input_i++];
          negative = true;
        }
        ptrdiff_t n = c - '0';
        while(input_i < input_size) {
          c = input[input_i++];
          if(isdigit(c)) {
            n *= 10;
            n += (c - '0');
          } else {
            input_i--;
            break;
          }
        }
        token_number = negative ? -n : n;
        tk = TK_NUMBER;
        return E_OK;
      } else if(isspace(c)) {
        // Skip whitespace
        continue;
      } else if(c == '\\') {
        // swallow comments
        while(input_i < input_size) {
          c = input[input_i++];
          if(c == '\n') {
            break;
          }
        }
      } else {
        // Word
        scratch_i = 1;
        scratch[0] = c;
        while(input_i < input_size) {
          c = input[input_i++];
          if(isspace(c)) { 
            break;
          }
          FT_CHECK(scratch_put(c));
        }
        FT_CHECK(scratch_put('\0'));
        tk = TK_WORD;
        return E_OK;
      }
    }
    tk = TK_END;
    return E_OK;
  }

  /** 
   * Execute arbitrary code
   */
  Error exec(const char* input_) {
    input = input_;
    input_size = strlen(input_);
    input_i = 0;

    Token tk;
    FT_CHECK(next_token(tk));
    while(tk != TK_END) {
      if(tk == TK_NUMBER) {
        // If interpreting, push directly
        if(*shared[S_COMPILING] == 0) {
          push(token_number);
        } else {
          // If compiling, push opcode
          dict_put(OP_PUSH_IMMEDIATE);
          dict_put(token_number);
        }
      } else if(tk == TK_WORD) {
        // We now have a word, look it up in the dictionary
        DictEntry* word = lookup(scratch);

        if(word) {
          if(*shared[S_COMPILING] == 0 && (word->flags & DictEntry::FLAG_COMPILE_ONLY)) {
            return E_COMPILE_ONLY;
          }

          // If in compilation and this is not an immediate word
          if(*shared[S_COMPILING] && (word->flags & DictEntry::FLAG_IMMEDIATE) == 0) {
            if(word->flags & DictEntry::FLAG_CWORD) {
              // Push c call followed by function pointer
              FT_CHECK(dict_put(OP_CALL_C));
              FT_CHECK(dict_put(*word->data<ptrdiff_t>()));
            } else {
              // Push forth call followed by pointer to forth VM code
              FT_CHECK(dict_put(OP_CALL_FORTH));
              FT_CHECK(dict_put(real_to_raddr(word->data<ptrdiff_t>())));
            }
          } else {
            // Either interpreting or this is an immediate word
            if(word->flags & DictEntry::FLAG_CWORD) {
              c_word_t cw;
              FT_CHECK(cword_get(*word->data<ptrdiff_t>(), cw));

              Error e = cw(*this);
              while(e == E_WANT_WORD) {
                // Attempt to read additional word name to pass through
                Token tk2;
                FT_CHECK(next_token(tk2));

                // Whatever we got wasn't a word, so just pass the error through
                if(tk2 != TK_WORD) return E_WANT_WORD;

                shared[S_WORD_AVAILABLE] = 1;

                e = cw(*this);
              }
              if(e != E_OK) {
                return e;
              }
            } else {
              // ptrdiff_t* raddr = (ptrdiff_t*) *word->data<ptrdiff_t>();
              FT_CHECK(exec((ptrdiff_t*) real_to_raddr(word->data<ptrdiff_t>())));
            }
          }
        } else {
          return E_WORD_NOT_FOUND;
        }
      }
      FT_CHECK(next_token(tk));
    }

    return E_OK;
  }

  /***** VIRTUAL MACHINE */

  // Convenience struct to restore to last locals after exiting function
  struct LocalsSave {
    LocalsSave(State& state_): state(state_), locals_i(state.locals.i) {}
    ~LocalsSave() {
      if(state.locals.i != locals_i) {
        state.locals.i = locals_i;
        FT_LOG(FT_VM, "% restored locals to " << locals_i);
      }
    }

    State& state;
    size_t locals_i;
  };

  /** Execute user defined Forth code */
  Error exec(ptrdiff_t* code_relative) {
#if FT_COMPUTED_GOTO
# define FT_VM_CASE(label) LABEL_##label
# define FT_VM_DISPATCH() goto *dispatch_table[code[ip++]];
# define FT_VM_SWITCH()
    static void* dispatch_table[] = {
      &&LABEL_OP_UNKNOWN,
      &&LABEL_OP_PUSH_IMMEDIATE,
      &&LABEL_OP_CALL_FORTH,
      &&LABEL_OP_CALL_C,
      &&LABEL_OP_JUMP_IF_ZERO,
      &&LABEL_OP_JUMP,
      &&LABEL_OP_JUMP_IGNORED,
      &&LABEL_OP_LOCAL_PUSH,
      &&LABEL_OP_LOCAL_SET,
      &&LABEL_OP_EXIT,
    };
#else 
# define FT_VM_CASE(label) case label
# define FT_VM_DISPATCH() break;
# define FT_VM_SWITCH() switch(code[ip++])
#endif
    FT_CHECKF(raddr_valid(code_relative), "exec got invalid address %ld", code_relative);
    ptrdiff_t* code = raddr_to_real(code_relative);
    // Save locals spot to clean up afterwards
    LocalsSave ls(*this);
    // TODO: Check that code addresses are valid memory.
    size_t ip = 0;

    while(true) {
      FT_VM_DISPATCH();
      FT_VM_SWITCH() {
        FT_VM_CASE(OP_PUSH_IMMEDIATE): {
          ptrdiff_t n = code[ip++];
          FT_LOG(FT_VM, "OP_PUSH_IMMEDIATE @ " << (size_t)&code[ip-2] << ' ' << n);
          push(n);
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_CALL_FORTH): {
          ptrdiff_t* next =  raddr_to_real((ptrdiff_t*) code[ip++]);
          FT_LOG(FT_VM, "OP_CALL_FORTH @ " << (size_t)&code[ip-2] << ' ' << (ptrdiff_t)next << " (relative) " << code[ip-1]);
          FT_CHECK(exec((ptrdiff_t*) code[ip-1]));
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_CALL_C): {
          c_word_t cw;
          FT_CHECK(cword_get(code[ip++], cw));
          FT_LOG(FT_VM, "OP_CALL_C @ " << (size_t)&code[ip-2] << ' ' << (size_t) cw);
          FT_CHECK(cw(*this));
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_EXIT): {
          FT_LOG(FT_VM, "OP_EXIT @ " << (size_t)&code[ip-1]);
          return E_OK;
        }
        FT_VM_CASE(OP_JUMP_IF_ZERO): {
          // Check stack
          if(si == 0) {
            return E_STACK_UNDERFLOW;
          }
          ptrdiff_t* label = (ptrdiff_t*) code[ip++];
          FT_LOG(FT_VM, "OP_JUMP_IF_ZERO @ " << (size_t)&code[ip-2] << ' ' << (size_t)label);
          si -= 1;
          if(stack[si].bits == 0) {
            FT_CHECK(raddr_valid((ptrdiff_t*)label));
            code = raddr_to_real(label);
            ip = 0;
          }
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_JUMP_IGNORED):
        FT_VM_CASE(OP_JUMP): {
          ptrdiff_t* label = (ptrdiff_t*) code[ip++];
          FT_LOG(FT_VM, "OP_JUMP @" << (size_t)&code[ip-1] << ' ' << label);
          ip = 0;
          FT_CHECK(raddr_valid(label));
          code = raddr_to_real(label);
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_LOCAL_PUSH): {
          // Push a local value onto the data stack
          ptrdiff_t local = code[ip++];
          ptrdiff_t actual = locals.i - local - 1;
          FT_LOG(FT_VM, "OP_LOCAL_PUSH @" << (size_t)&code[ip-1] << ' ' << local << " (actual " << actual << ")")

          FT_CHECK(push(locals.data[actual]));
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_LOCAL_SET): {
          FT_LOG(FT_VM, "OP_LOCAL_SET");
          Cell val;
          FT_CHECK(pop(val));

          FT_CHECK(locals.push(val.bits));
          FT_VM_DISPATCH();
        }
        FT_VM_CASE(OP_UNKNOWN): {
          FT_LOG(FT_VM, "E_INVALID_OPCODE @ " << (size_t)&code[ip-1] << ' ' << code[ip-1]);
          return E_INVALID_OPCODE;
        }
      }
    }
    return E_OK;
  }
};

}; // namespace ft

#endif