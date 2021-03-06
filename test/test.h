#ifndef _TEST_TEST_H_
#define _TEST_TEST_H_

#include <candor.h>
#include <heap.h>
#include <heap-inl.h>
#include <zone.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <assert.h>

using namespace candor;
using namespace internal;

#define ASSERT(expr) \
    do { \
     if (!(expr)) { \
       fprintf(stderr, \
               "Assertion failed in %s on line %d: %s\n", \
               __FILE__, \
               __LINE__, \
               #expr); \
       abort(); \
     } \
    } while (0);

#define TEST_START(name)\
    void __test_runner_##name() {\

#define TEST_END(name)\
    }

#define PARSER_TEST(code, expected)\
    {\
      Zone z;\
      char out[1024];\
      Parser p(code, strlen(code));\
      AstNode* ast = p.Execute();\
      if (p.has_error()) {\
        fprintf(stdout, "%d:%s\n", p.error_pos(), p.error_msg());\
        ASSERT(!p.has_error());\
      }\
      p.Print(out, 1000);\
      ASSERT(ast != NULL);\
      if (strcmp(expected, out) != 0) {\
        fprintf(stderr, "PARSER test failed, got:\n%s\n expected:\n%s\n",\
                out,\
                expected);\
        abort();\
      }\
      ast = NULL;\
    }

#define SCOPE_TEST(code, expected)\
    {\
      Zone z;\
      char out[1024];\
      Parser p(code, strlen(code));\
      AstNode* ast = p.Execute();\
      ASSERT(!p.has_error());\
      Scope::Analyze(ast);\
      p.Print(out, sizeof(out));\
      ASSERT(ast != NULL);\
      if (strcmp(expected, out) != 0) {\
        fprintf(stderr, "SCOPE test failed, got:\n%s\n expected:\n%s\n",\
                out,\
                expected);\
        abort();\
      }\
      ast = NULL;\
    }

#define FULLGEN_TEST(code, expected)\
    {\
      Zone z;\
      char out[10024];\
      Heap heap(2 * 1024 * 1024);\
      Root root(&heap);\
      Parser p(code, strlen(code));\
      AstNode* ast = p.Execute();\
      ASSERT(!p.has_error());\
      Scope::Analyze(ast);\
      ASSERT(ast != NULL);\
      Fullgen gen(&heap, &root, "test");\
      gen.Build(ast);\
      gen.Print(out, sizeof(out));\
      if (strcmp(expected, out) != 0) {\
        fprintf(stderr, "Fullgen test failed, got:\n%s\n expected:\n%s\n",\
                out,\
                expected);\
        abort();\
      }\
      ast = NULL;\
    }

#define HIR_TEST(code, expected)\
    {\
      Zone z;\
      char out[10024];\
      Heap heap(2 * 1024 * 1024);\
      Root root(&heap);\
      Parser p(code, strlen(code));\
      AstNode* ast = p.Execute();\
      ASSERT(!p.has_error());\
      Scope::Analyze(ast);\
      ASSERT(ast != NULL);\
      HIRGen gen(&heap, &root, NULL);\
      gen.Build(ast);\
      gen.Print(out, sizeof(out));\
      if (strcmp(expected, out) != 0) {\
        fprintf(stderr, "HIR test failed, got:\n%s\n expected:\n%s\n",\
                out,\
                expected);\
        abort();\
      }\
      ast = NULL;\
    }

#define LIR_TEST(code, expected)\
    {\
      Zone z;\
      char out[10024];\
      Heap heap(2 * 1024 * 1024);\
      Root root(&heap);\
      Parser p(code, strlen(code));\
      AstNode* ast = p.Execute();\
      ASSERT(!p.has_error());\
      Scope::Analyze(ast);\
      ASSERT(ast != NULL);\
      HIRGen hgen(&heap, &root, NULL);\
      hgen.Build(ast);\
      LGen lgen(&hgen, NULL, hgen.roots()->head()->value());\
      lgen.Print(out, sizeof(out));\
      if (strcmp(expected, out) != 0) {\
        fprintf(stderr, "LIR test failed, got:\n%s\n expected:\n%s\n",\
                out,\
                expected);\
        abort();\
      }\
      ast = NULL;\
    }

#define FUN_TEST(code, block)\
    {\
      Isolate i;\
      Function* f = Function::New("test", code, strlen(code));\
      if (i.HasError()) {\
        i.PrintError();\
        abort();\
      }\
      Value* result = f->Call(0, NULL);\
      block\
    }

#define BENCH_START(name, num)\
    timeval __bench_##name##_start;\
    gettimeofday(&__bench_##name##_start, NULL);

#define BENCH_END(name, num)\
    timeval __bench_##name##_end;\
    gettimeofday(&__bench_##name##_end, NULL);\
    double __bench_##name##_total = __bench_##name##_end.tv_sec -\
                                    __bench_##name##_start.tv_sec +\
                                    __bench_##name##_end.tv_usec * 1e-6 -\
                                    __bench_##name##_start.tv_usec * 1e-6;\
    if ((num) != 0) {\
      fprintf(stdout, #name " : %f ops/sec\n",\
              (num) / __bench_##name##_total);\
    } else {\
      fprintf(stdout, #name " : %fs\n",\
              __bench_##name##_total);\
    }

#endif //  _TEST_TEST_H_
