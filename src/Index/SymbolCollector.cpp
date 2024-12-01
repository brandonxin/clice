#include "SymbolBuilder.h"

namespace clice::index {

namespace {

class SymbolCollector : public clang::RecursiveASTVisitor<SymbolCollector> {
public:
    using Base = clang::RecursiveASTVisitor<SymbolCollector>;

    SymbolCollector(SymbolBuilder& builder, clang::ASTContext& context) :
        builder(builder), context(context), srcMgr(context.getSourceManager()) {}

public:
    /// ============================================================================
    ///                                Declaration
    /// ============================================================================

    VISIT_DECL(NamespaceDecl) {
        /// `namespace Foo { }`
        ///             ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        return true;
    }

    VISIT_DECL(NamespaceAliasDecl) {
        /// `namespace Foo = Bar`
        ///             ^     ^~~~ reference
        ///             ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        if(auto location = builder.addLocation(decl->getTargetNameLoc())) {
            auto symbol = builder.addSymbol(decl->getNamespace());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_DECL(UsingDirectiveDecl) {
        /// `using namespace Foo`
        ///                   ^~~~~~~ reference
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl->getNominatedNamespace());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_DECL(LabelDecl) {
        /// `label:`
        ///    ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        return true;
    }

    VISIT_DECL(FieldDecl) {
        /// `struct Foo { int bar; };`
        ///                    ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
            symbol.addTypeDefinition(decl->getType());
        }
        return true;
    }

    VISIT_DECL(EnumConstantDecl) {
        /// `enum Foo { bar = 1 };`
        ///              ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
            symbol.addTypeDefinition(decl->getType());
        }
        return true;
    }

    /// FIXME: how to resolve shadow decls? a name could refer to multiple decls.
    /// Don't need to handle UsingEnumDecl, it's handled by VisitTypeLoc.
    VISIT_DECL(UsingDecl) {
        return true;
    }

    VISIT_DECL(BindingDecl) {
        /// `auto [a, b] = std::make_tuple(1, 2);`
        ///        ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
            symbol.addTypeDefinition(decl->getType());
        }
        return true;
    }

    VISIT_DECL(TemplateTypeParmDecl) {
        /// `template <typename T> ...`
        ///                     ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        return true;
    }

    VISIT_DECL(TemplateTemplateParmDecl) {
        /// `template <template <typename> class T> ...`
        ///                                      ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        return true;
    }

    VISIT_DECL(NonTypeTemplateParmDecl) {
        /// `template <int N> ...`
        ///                ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
            symbol.addTypeDefinition(decl->getType());
        }
        return true;
    }

    VISIT_DECL(TagDecl) {
        /// `Base::TraverseClassTemplate...Decl` will also traverse it's templated class.
        /// We already handled the template class in `VisitClassTemplate...Decl`. So we skip
        /// them here.
        if(decl->getDescribedTemplate() ||
           llvm::isa<clang::ClassTemplateSpecializationDecl,
                     clang::ClassTemplatePartialSpecializationDecl>(decl)) {
            return true;
        }

        /// `struct/class/union/enum Foo { };`
        ///                           ^~~~ declaration/definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
        }
        return true;
    }

    VISIT_DECL(ClassTemplateDecl) {
        /// `template <typename T> class Foo { };`
        ///                               ^~~~ declaration/definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
        }
        return true;
    }

    VISIT_DECL(ClassTemplateSpecializationDecl) {
        /// It's possible that a class template specialization is a full specialization or a
        /// explicit instantiation. And `ClassTemplatePartialSpecializationDecl` is the subclass of
        /// `ClassTemplateSpecializationDecl`, it is also handled here.
        ///
        /// For full specialization:
        /// `template <> class Foo<int> { };`
        ///                     ^~~~ declaration/definition
        ///
        /// For explicit instantiation:
        /// `template class Foo<int>;`
        ///                  ^~~~ reference
        if(auto location = builder.addLocation(decl->getLocation())) {
            assert(decl->getSpecializationKind() != clang::TSK_ImplicitInstantiation &&
                   "Implicit instantiation should not be handled here");
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            if(decl->isExplicitSpecialization()) {
                symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
            } else {
                symbol.addReference(location);
            }
        }
        return true;
    }

    VISIT_DECL(FunctionDecl) {
        /// Because `TraverseFunctionTemplateDecl` will also traverse it's templated function. We
        /// already handled the template function in `VisitFunctionTemplateDecl`. So we skip them
        /// here.
        if(decl->getDescribedFunctionTemplate()) {
            return true;
        }

        /// `void foo();`
        ///         ^~~~ declaration/definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);

            if(auto CMD = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                // FIXME: virtual override const ...

                if(auto CCD = llvm::dyn_cast<clang::CXXConstructorDecl>(CMD)) {
                    symbol.addTypeDefinition(CCD->getThisType());
                }

                // FIXME: CXXConversionDecl, CXXDestructorDecl
            }

            // FIXME: CXXDeductionGuideDecl
        }
        currentFunction = decl;
        return true;
    }

    VISIT_DECL(FunctionTemplateDecl) {
        /// `template <typename T> void foo();`
        ///                               ^~~~ declaration/definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
        }

        /// FIXME: Clang currently doesn't record the information of explicit instantiation
        /// correctly. See https://github.com/llvm/llvm-project/issues/115418. And it is not added
        /// to its lexical context. So here we use a workaround to handle explicit instantiation of
        /// function template.
        if(builder.alreadyVisited(decl)) {
            return true;
        }

        for(auto spec: decl->specializations()) {
            /// `template void foo<int>();`
            ///                  ^~~~ reference
            auto kind = spec->getTemplateSpecializationKind();
            if(kind == clang::TSK_ExplicitInstantiationDeclaration ||
               kind == clang::TSK_ExplicitInstantiationDefinition) {
                /// WORKAROUND: Clang currently doesn't record the location of explicit
                /// instantiation. Use the location of the point of instantiation instead.
                if(auto location = builder.addLocation(spec->getPointOfInstantiation())) {
                    auto symbol = builder.addSymbol(decl);
                    symbol.addOccurrence(location);
                    symbol.addReference(location);
                }
            }
        }

        return true;
    }

    VISIT_DECL(TypedefNameDecl) {
        /// Skip the typedef name declaration that is a type alias template.
        if(decl->getDescribedTemplate()) {
            return true;
        }

        /// `typedef int Foo;`
        ///               ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
            symbol.addTypeDefinition(decl->getUnderlyingType());
        }
        return true;
    }

    VISIT_DECL(TypeAliasTemplateDecl) {
        /// FIXME: the location of type alias template is not recorded correctly, actually
        /// it is the location of using keyword. But location its templated type is correct.
        /// Temporarily use the location of the templated type.

        /// `template <typename T> using Foo = T;`
        ///                               ^~~~ definition
        if(auto location = builder.addLocation(decl->getTemplatedDecl()->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        return true;
    }

    VISIT_DECL(VarDecl) {
        /// Because `TraverseVarTemplateSpecializationDecl` will also traverse it's templated
        /// variable. We already handled the template variable in `VisitVar...`.
        /// So we skip them here.
        if(decl->getDescribedVarTemplate() ||
           llvm::isa<clang::VarTemplateSpecializationDecl,
                     clang::VarTemplatePartialSpecializationDecl>(decl)) {
            return true;
        }

        /// `int foo;`
        ///       ^~~~ declaration/definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
            symbol.addTypeDefinition(decl->getType());
        }
        return true;
    }

    VISIT_DECL(VarTemplateDecl) {
        /// `template <typename T> int foo;`
        ///                             ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
        }
        return true;
    }

    VISIT_DECL(VarTemplateSpecializationDecl) {
        /// FIXME: it's strange that `VarTemplateSpecializationDecl` occurs in the lexical context.
        /// This should be a bug of clang. Skip it here. And clang also doesn't record the
        /// information about explicit instantiation of var template correctly. Skip them
        /// temporarily.
        auto kind = decl->getSpecializationKind();
        if(kind == clang::TSK_ImplicitInstantiation ||
           kind == clang::TSK_ExplicitInstantiationDeclaration ||
           kind == clang::TSK_ExplicitInstantiationDefinition) {
            return true;
        }

        /// It's possible that a var template specialization is a full specialization or a explicit
        /// instantiation. And `VarTemplatePartialSpecializationDecl` is the subclass of
        /// `VarTemplateSpecializationDecl`, it is also handled here.
        ///
        /// For full specialization:
        /// `template <> int foo<int>;`
        ///                    ^~~~ declaration/definition
        ///
        /// For explicit instantiation:
        /// `template int foo<int>;`
        ///                ^~~~ reference
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            if(decl->isExplicitSpecialization()) {
                symbol.addDeclarationOrDefinition(decl->isThisDeclarationADefinition(), location);
            } else {
                symbol.addReference(location);
            }
        }
        return true;
    }

    VISIT_DECL(ConceptDecl) {
        /// `template <typename T> concept Foo = ...;`
        ///                                 ^~~~ definition
        if(auto location = builder.addLocation(decl->getLocation())) {
            auto symbol = builder.addSymbol(decl);
            symbol.addOccurrence(location);
            symbol.addDefinition(location);
        }
        return true;
    }

    bool TraverseConceptReference(clang::ConceptReference* reference) {
        /// `requires Foo<T>;`
        ///            ^~~~ reference
        if(auto location = builder.addLocation(reference->getConceptNameLoc())) {
            auto symbol = builder.addSymbol(reference->getNamedConcept());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }

        return Base::TraverseConceptReference(reference);
    }

    /// ============================================================================
    ///                                  TypeLoc
    /// ============================================================================

    /// We don't care about type without location information.
    constexpr bool TraverseType [[gnu::const]] (clang::QualType) {
        return true;
    }

    VISIT_TYPELOC(TagTypeLoc) {
        /// `struct Foo { }; Foo foo`
        ///                   ^~~~ reference
        if(auto location = builder.addLocation(loc.getNameLoc())) {
            auto symbol = builder.addSymbol(loc.getDecl());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_TYPELOC(TypedefTypeLoc) {
        /// `using Foo = int; Foo foo`
        ///                    ^~~~ reference
        if(auto location = builder.addLocation(loc.getNameLoc())) {
            auto symbol = builder.addSymbol(loc.getTypedefNameDecl());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_TYPELOC(TemplateTypeParmTypeLoc) {
        /// `template <typename T> void foo(T t)`
        ///                                 ^~~~ reference
        if(auto location = builder.addLocation(loc.getNameLoc())) {
            auto symbol = builder.addSymbol(loc.getDecl());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_TYPELOC(TemplateSpecializationTypeLoc) {
        /// `std::vector<int> foo`
        ///        ^~~~ reference
        if(auto location = builder.addLocation(loc.getTemplateNameLoc())) {
            auto symbol = builder.addSymbol(declForType(loc.getType()));
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_TYPELOC(DependentNameTypeLoc) {
        loc.getNameLoc().dump(srcMgr);
        return true;
    }

    VISIT_TYPELOC(DependentTemplateSpecializationTypeLoc) {
        return true;
    }

    /// ============================================================================
    ///                                Specifier
    /// ============================================================================

    bool TraverseCXXBaseSpecifier(const clang::CXXBaseSpecifier& base) {
        return Base::TraverseCXXBaseSpecifier(base);
    }

    /// We don't care about name specifier without location information.
    constexpr bool TraverseNestedNameSpecifier [[gnu::const]] (clang::NestedNameSpecifier*) {
        return true;
    }

    bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc loc) {
        if(auto location = builder.addLocation(loc.getLocalSourceRange())) {
            auto NNS = loc.getNestedNameSpecifier();
            switch(NNS->getKind()) {
                case clang::NestedNameSpecifier::Namespace: {
                    auto symbol = builder.addSymbol(NNS->getAsNamespace());
                    symbol.addOccurrence(location);
                    symbol.addReference(location);
                    break;
                }
                case clang::NestedNameSpecifier::NamespaceAlias: {
                    auto symbol = builder.addSymbol(NNS->getAsNamespaceAlias());
                    symbol.addOccurrence(location);
                    symbol.addReference(location);
                    break;
                }
                case clang::NestedNameSpecifier::Identifier: {
                    assert(NNS->isDependent() && "Identifier NNS should be dependent");
                    // FIXME: use TemplateResolver here.
                    break;
                }
                case clang::NestedNameSpecifier::TypeSpec:
                case clang::NestedNameSpecifier::TypeSpecWithTemplate:
                case clang::NestedNameSpecifier::Global:
                case clang::NestedNameSpecifier::Super: {
                    break;
                };
            }
        }

        return Base::TraverseNestedNameSpecifierLoc(loc);
    }

    /// ============================================================================
    ///                                 Statement
    /// ============================================================================

    VISIT_EXPR(DeclRefExpr) {
        /// `foo = 1`
        ///   ^~~~ reference
        if(auto location = builder.addLocation(expr->getLocation())) {
            auto symbol = builder.addSymbol(expr->getDecl());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    VISIT_EXPR(MemberExpr) {
        /// `foo.bar`
        ///       ^~~~ reference
        if(auto location = builder.addLocation(expr->getMemberLoc())) {
            auto symbol = builder.addSymbol(expr->getMemberDecl());
            symbol.addOccurrence(location);
            symbol.addReference(location);
        }
        return true;
    }

    // FIXME: modify template resolver to cache recursively resolved results.
    VISIT_EXPR(DependentScopeDeclRefExpr) {
        return true;
    }

    VISIT_EXPR(CXXDependentScopeMemberExpr) {
        return true;
    }

    VISIT_EXPR(CallExpr) {
        // TODO: consider lambda expression.
        return true;
    }

    bool TraverseConstructorInitializer(clang::CXXCtorInitializer* init) {
        return Base::TraverseConstructorInitializer(init);
    }

private:
    SymbolBuilder& builder;
    clang::ASTContext& context;
    clang::SourceManager& srcMgr;

    const clang::FunctionDecl* currentFunction = nullptr;
};

void sortSymbols(std::vector<memory::Symbol>& symbols, std::vector<Occurrence>& occurrences) {
    const auto size = symbols.size();
    std::vector<uint32_t> indices;
    indices.reserve(size);
    for(std::size_t index = 0; index < size; index++) {
        indices.emplace_back(index);
    }

    llvm::sort(indices,
               [&](uint32_t lhs, uint32_t rhs) { return symbols[lhs].id < symbols[rhs].id; });

    std::vector<uint32_t> map;
    map.resize(size);
    for(std::size_t index = 0; index < size; index++) {
        /// original index -> new index
        map[indices[index]] = index;
    }

    /// Adjust the index in occurrences and relations.
    for(auto& occurrence: occurrences) {
        occurrence.symbol.offset = map[occurrence.symbol.offset];
    }

    for(auto& symbol: symbols) {
        for(auto& relation: symbol.relations) {
            // if(Relation::isSymbol(relation)) {
            //     relation.symOrLoc = map[relation.symOrLoc];
            // }
        }
    }

    /// Sort symbols.
    llvm::sort(symbols, [](const memory::Symbol& lhs, const memory::Symbol& rhs) {
        return lhs.id < rhs.id;
    });
}

void sortFiles(std::vector<memory::File>& files, std::vector<Location>& locations) {
    const auto size = files.size();
    std::vector<uint32_t> indices;
    indices.reserve(size);
    for(std::size_t index = 0; index < size; index++) {
        indices.emplace_back(index);
    }

    llvm::sort(indices,
               [&](uint32_t lhs, uint32_t rhs) { return files[lhs].path < files[rhs].path; });

    std::vector<uint32_t> map;
    map.resize(size);
    for(std::size_t index = 0; index < size; index++) {
        /// original index -> new index
        map[indices[index]] = index;
    }

    /// Adjust the index in files and .
    for(auto& location: locations) {
        if(location.file.isValid()) {
            location.file = FileRef{map[location.file.offset]};
        }
    }

    /// Sort files.
    llvm::sort(files, [](const memory::File& lhs, const memory::File& rhs) {
        return lhs.path < rhs.path;
    });
}

void SymbolBuilder::indexTU(memory::Index& result, ASTInfo& compiler) {
    index = &result;
    SymbolCollector collector(*this, compiler.tu()->getASTContext());
    collector.TraverseAST(compiler.context());

    /// Beacuse we store the index of symbols in Occurrence and Relation, Sorting symbols will
    /// invalidate the index. So we need to adjust the them. First, we need to obtain the index
    /// mapping before and after sorting.

    sortFiles(result.files, result.locations);
    sortSymbols(result.symbols, result.occurrences);

    llvm::sort(result.occurrences, [&](const Occurrence& lhs, const Occurrence& rhs) {
        if(lhs.location.isInvalid() || rhs.location.isInvalid()) {
            std::terminate();
        }
        return result.locations[lhs.location.offset] < result.locations[rhs.location.offset];
    });
}

}  // namespace

memory::Index index(ASTInfo& compiler) {
    memory::Index result;
    SymbolBuilder builder(compiler.sema(), compiler.tokBuf());
    builder.indexTU(result, compiler);
    return std::move(result);
};  // namespace clice::index

}  // namespace clice::index