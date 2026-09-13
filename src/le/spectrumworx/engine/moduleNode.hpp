////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleNode.hpp
/// --------------------
///
/// Copyright (c) 2012 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleNode_hpp__926B45C3_7354_4519_AE90_42C1965E9F77
#define moduleNode_hpp__926B45C3_7354_4519_AE90_42C1965E9F77
//------------------------------------------------------------------------------
#include "le/utility/parentFromMember.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/referenceCounter.hpp"

#include "le/utility/polymorphicDowncast.hpp"
#include "le/utility/intrusivePtr.hpp"

#include <type_traits>

namespace LE::SW
{

namespace Engine
{

class ModuleNode
{
  public:
    using NodePtr = LE::Utility::IntrusivePtr<ModuleNode>;
    using NodeCPtr = LE::Utility::IntrusivePtr<ModuleNode const>;

    static_assert(sizeof(NodePtr) == sizeof(void *), "");

    mutable Utility::ReferenceCount referenceCount_;

    mutable NodePtr next_;
    mutable NodePtr previous_;

  protected:
    ModuleNode() = default;

#if !defined(NDEBUG)
  protected:
    ~ModuleNode() = default;
    virtual void rtti_enforcer() {}
#endif // NDEBUG

    ModuleNode(ModuleNode const &) = delete;
    ModuleNode(ModuleNode &&) = delete;
}; // class ModuleNode

void intrusive_ptr_add_ref(ModuleNode const *);
void intrusive_ptr_release_deleter(ModuleNode const *);

inline void intrusive_ptr_release(ModuleNode const *LE_RESTRICT const pModuleNode)
{
    LE_ASSERT(pModuleNode);
    if (!--pModuleNode->referenceCount_) [[unlikely]]
    {
        intrusive_ptr_release_deleter(pModuleNode);
    }
}

template <class ActualModule> ActualModule &actualModule(ModuleNode &node)
{
    auto *LE_RESTRICT const pModule(LE::Utility::polymorphicDowncast<ActualModule *>(&node));
    LE_ASSERT(pModule);
    return *pModule;
}
template <class ActualModule> ActualModule const &actualModule(ModuleNode const &node)
{
    return actualModule<ActualModule>(const_cast<ModuleNode &>(node));
}

template <class ActualModule> ModuleNode &node(ActualModule &module)
{
    auto *LE_RESTRICT const pNode(LE::Utility::polymorphicDowncast<ModuleNode *>(&module));
    LE_ASSERT(pNode);
    return *pNode;
}
template <class ActualModule> ModuleNode const &node(ActualModule const &chainedModule)
{
    return node(const_cast<ActualModule &>(chainedModule));
}

} // namespace Engine

template <class ActualModule>
LE_FORCEINLINE void intrusive_ptr_add_ref(ActualModule const *const pModule)
{
    intrusive_ptr_add_ref(&Engine::node(*pModule));
}
template <class ActualModule>
LE_FORCEINLINE void intrusive_ptr_release(ActualModule const *const pModule)
{
    intrusive_ptr_release(&Engine::node(*pModule));
}

} // namespace LE::SW

#endif // moduleNode_hpp
