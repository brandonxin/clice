#pragma once

#include "../Test.h"
#include "Annotation.h"

#include "Compiler/Compiler.h"
#include "Index/Loader.h"

namespace clice {

using namespace index;

/// Recursively test the binary index has the totally same content
/// with the original index.
template <typename In, typename Out>
void testEqual(const Loader& loader, const In& in, const Out& out) {
    if constexpr(requires { in == out; }) {
        EXPECT_TRUE(in == out);
    } else if constexpr(std::is_same_v<Out, binary::string>) {
        EXPECT_EQ(in, loader.string(out));
    } else if constexpr(is_specialization_of<Out, binary::array>) {
        using value_type = typename Out::value_type;
        auto array = loader.make_range(out);
        for(std::size_t i = 0; i < in.size(); i++) {
            testEqual(loader, in[i], array[i]);
        }
    } else {
        refl::foreach(in, out, [&](const auto& lhs, const auto& rhs) {
            testEqual(loader, lhs, rhs);
        });
    }
}

struct IndexerTester {
    Annotation annotation;
    std::unique_ptr<Loader> loader;
    std::unique_ptr<ASTInfo> compiler;
    FileRef mainFile;

    IndexerTester(llvm::StringRef source, bool json = false) : annotation(source) {
        std::vector<const char*> args = {
            "clang++",
            "-std=c++20",
            "main.cpp",
            "-resource-dir",
            "/home/ykiko/C++/clice2/build/lib/clang/20",
        };

        /// FIXME:
        // compiler = std::make_unique<Compiler>("main.cpp", annotation.source(), args);
        // compiler->buildAST();

        auto index = index::index(*compiler);

        if(json) {
            std::error_code error;
            llvm::raw_fd_ostream file("index.json", error);
            file << index::toJson(index);
        }

        loader = std::make_unique<Loader>(index::toBinary(index));

        /// Test equal here.
        testEqual(*loader, index, loader->getIndex());

        auto files = loader->locateFile("main.cpp");
        assert(files.size() == 1);
        mainFile = {static_cast<uint32_t>(files.begin() - loader->files().begin())};
    }

    void GotoDefinition(llvm::StringRef position) {
        auto loc = annotation.position(position);
        auto sym = loader->locateSymbol(mainFile, loc);
        bool success = loader->lookupRelation(sym,
                                              RelationKind::Definition,
                                              [&](const FullLocation& location) {
                                                  llvm::outs() << json::serialize(location) << "\n";
                                              });
        EXPECT_TRUE(success);
    }

    IndexerTester& GotoDefinition(llvm::StringRef position,
                                  llvm::StringRef target,
                                  std::source_location info = std::source_location::current()) {
        auto loc = annotation.position(position);
        auto sym = loader->locateSymbol(mainFile, loc);
        bool success =
            loader->lookupRelation(sym,
                                   RelationKind::Definition,
                                   [&](const FullLocation& location) {
                                       EXPECT_EQ(location.begin, annotation.position(target));
                                   });
        EXPECT_TRUE(success);
        return *this;
    }
};

}  // namespace clice