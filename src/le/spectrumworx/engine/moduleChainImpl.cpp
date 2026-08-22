////////////////////////////////////////////////////////////////////////////////
///
/// moduleChainImpl.cpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleChainImpl.hpp"

#include "module.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace LE::SW::Engine
{

ModuleChainBase::ModuleChainBase()
{
    referenceCount_.verifyCountEqual(0);
    ++referenceCount_;
    referenceCount_.verifyCountEqual(1);
    resetRoot();
    referenceCount_.verifyCountEqual(3);
}

ModuleChainBase::ModuleChainBase(ModuleChainBase &&other)
{
    referenceCount_.verifyCountEqual(0);
    ++referenceCount_;
    referenceCount_.verifyCountEqual(1);
    resetRoot();
    referenceCount_.verifyCountEqual(3);

    moveAssign(std::forward<ModuleChainBase>(other));
}

ModuleChainBase &ModuleChainBase::operator=(ModuleChainBase &&other)
{
    clear();
    moveAssign(std::forward<ModuleChainBase>(other));
    return *this;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Because nodes form cyclic references through next and previous
/// pointers, each node has to be explicitly unlinked in order to avoid leaks.
///                                           (12.03.2014.) (Domagoj Saric)
///
/// \note `clear()` is not enough, and was for as long as a chain outlived every
/// module that had ever been in it. `unlink()` writes the *neighbours*' links and
/// deliberately leaves the removed node's own -- so every ex-member still holds a
/// counted pointer at this root. That cost nothing while the chain was the last
/// owner: the modules were destroyed on the spot and released it on the way out.
///
///   Stage 6 made chains that die first. A preset load builds a chain, publishes
/// it, and the one it displaces comes back to be destroyed here -- while the
/// editor's strips still hold counted references to the modules that were in it.
/// The root was then freed with those pointers still aimed at it: this
/// destructor's own assertion is what caught it, and what it caught was a
/// use-after-free waiting for the user to close a strip.
///
///   So the links go too. Nothing walks a chain that is being destroyed, which is
/// the one thing the note on `remove()` says they are kept for.
///
////////////////////////////////////////////////////////////////////////////////

ModuleChainBase::~ModuleChainBase()
{
    while (next_.get() != this)
    {
        /// \note Held across the unlink, because the unlink drops the chain's
        /// reference and this may be the last one.
        NodePtr const pNode(next_);
        node_algorithms::unlink(pNode.get());
        pNode->next_.reset();
        pNode->previous_.reset();
    }
    LE_ASSERT(empty());
    LE_ASSERT(this->referenceCount_ == 1 || this->referenceCount_ == 3);
}

void ModuleChainBase::moveAssign(ModuleChainBase &&other)
{
    LE_ASSERT(this->empty());

#ifndef NDEBUG
    auto const otherSize(other.size());
#endif // NDEBUG

    Node *const begin(other.next_.get());
    LE_ASSERT(&*begin == other.begin());
    Node *const end(&other);
    LE_ASSERT(&*end == other.end());
    node_algorithms::transfer(this, begin, end);
    //this->previous_ = other.previous_;
    //this->next_     = other.next_    ;
    //other.resetRoot();
    LE_ASSERT(other.referenceCount_ == 3);
    LE_ASSERT(other.empty());
    LE_ASSERT(this->size() == otherSize);
}

std::uint8_t ModuleChainBase::getIndexForModule(Node const &module) const
{
#if 0
    LE_ASSERT_MSG
    (
        std::find_if
        (
            begin(),
            end  (),
            [&]( Node const & other ) { return &other == &module; }
        ) != end(),
        "Module not in chain."
    );
    return static_cast<std::uint8_t>( std::distance( begin(), iterator_to( module ) ) );
#elif 1
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Bounded by the chain's own length, where the bound used to be an
    /// `LE_ASSERT_MSG` -- which is not compiled in a shipped build, so a module
    /// that had left this chain sent the walk round a circular list **forever**.
    /// Reachable from a focus change on a strip whose module has just been
    /// removed, which is a hung interface rather than a crash and so is the
    /// harder kind to report.
    ///
    ///   The bound is `size()` inclusive rather than exclusive because the root
    /// node is a legitimate answer: `module.next_` is the root when \p module is
    /// the last in the chain, and matching it there is what makes the index
    /// right. (Reading the *next* node is the 2016 trick for a module that has
    /// already been unlinked; it works only while the unlinked node still points
    /// back into this chain, which is why there has to be an end to the search.)
    ///
    /// \note And a value rather than a hang when the answer is no. Every caller
    /// puts it straight into a `ParameterID`, where a module index past the end
    /// is already handled -- `isValidParamId()` refuses it and `moduleAs()`
    /// answers null -- so the edit is dropped, which is what an edit against a
    /// module that is not in the chain should be.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const nodes(this->size());
    unsigned int index(0);
    for (iterator pCurrentModule(this->begin()); index <= nodes; ++pCurrentModule, ++index)
    {
        //...mrmlj...ugh...use the next node to handle the case when this module
        //is already removed...
        if (pCurrentModule.get() == module.next_.get())
            return static_cast<std::uint8_t>(index - 1);
    }

    LE_ASSERT_MSG(false, "Module not from this chain!");
    return notInChain;
#endif // impl
}

ModuleChainBase::iterator ModuleChainBase::module(std::uint8_t index)
{
    LE_ASSERT_MSG(!dynamic_cast<Engine::ModuleDSP const *>(end().get()),
                  "Root node is not supposed to be an actual module.");
    iterator pCurrentModule(this->begin());
    while (index && !isEnd(pCurrentModule))
    {
        --index;
        ++pCurrentModule;
    }
    return pCurrentModule;
}
ModuleChainBase::const_iterator ModuleChainBase::module(std::uint8_t const index) const
{
    return const_cast<ModuleChainBase &>(*this).module(index);
}

void ModuleChainBase::clear()
{
    iterator pCurrentModule(this->begin());
    while (pCurrentModule != pCurrentModule.get()->next_)
    {
        pCurrentModule = node_algorithms::unlink(pCurrentModule.get());
    }
    resetRoot();
}

void ModuleChainBase::swap(ModuleChainBase &other)
{
    /// \note A scratch chain on the stack, which allocates nothing: the
    /// constructor links the root node to itself and the destructor clears an
    /// already empty list. `moveAssign` requires an empty destination, which each
    /// of the three steps has by construction.
    ModuleChainBase scratch;
    scratch.moveAssign(std::move(other));
    other.moveAssign(std::move(*this));
    this->moveAssign(std::move(scratch));
}

void ModuleChainBase::moveModule(std::uint8_t const sourceIndex, std::uint8_t const targetIndex)
{
    LE_ASSUME(sourceIndex != targetIndex);
    //LE_ASSUME( sourceIndex < Constants::maxNumberOfModules );
    //LE_ASSUME( targetIndex < Constants::maxNumberOfModules );
    iterator const pSource(module(sourceIndex));
    iterator const pTarget(module(targetIndex));
    LE_ASSERT(pSource != end());
    LE_ASSERT(pTarget != end());
    node_algorithms::unlink(pSource.get());
    if (sourceIndex < targetIndex)
        node_algorithms::link_after(pTarget.get(), pSource.get());
    else
        node_algorithms::link_before(pTarget.get(), pSource.get());
    //...mrmlj...
    //boost::swap( pSource->next_    , pTarget->next_     );
    //boost::swap( pSource->previous_, pTarget->previous_ );
    //insert( erase( iterator_to( *pSource ) ), *pTarget );
    //insert( erase( iterator_to( *pTarget ) ), *pSource );
}

void ModuleChainBase::push_back(Node &module)
{
    //...mrmlj...LE_ASSERT_MSG( node_algorithms::inited( &module ), "Module already belongs to a different chain." );
    node_algorithms::link_before(this, &module);
}

void ModuleChainBase::insertAtAndReplace(iterator const &pInsertPosition, Node &moduleToInsert)
{
    LE_ASSERT_MSG(node_algorithms::inited(&moduleToInsert),
                  "Module already belongs to a different chain.");
    node_algorithms::link_before(pInsertPosition.get(), &moduleToInsert);
    if (!isEnd(pInsertPosition))
        remove(*pInsertPosition);
}

void ModuleChainBase::remove(Node &node)
{
#ifndef NDEBUG
    LE_ASSERT_MSG(&node != this, "You can't remove the root node.");
    auto const previous(node.previous_);
    auto const next(node.next_);
    bool const referenced(node.referenceCount_ > 2);
    LE_ASSERT(previous_);
    LE_ASSERT(next_);
#endif // NDEBUG
    node_algorithms::unlink(&node);
#ifndef NDEBUG
    /// \note The unlink procedure must leave the unlinked node's previous and
    /// next pointers intact in case another thread is using the node and wishes
    /// to continue on to the next one after it finishes with the one being
    /// unlinked.
    ///                                       (11.09.2014.) (Domagoj Saric)
    LE_ASSERT((node.previous_ == previous && node.next_ == next) || !referenced);
#endif // NDEBUG
}

/// \note Without the `LE_ASSUME( &module )` both of these opened with: the
/// address of a reference is not null by the language's own rules, so the
/// assumption told the optimiser nothing and GCC 15 reported it as a nonnull
/// argument compared to NULL.
ModuleChainBase::iterator ModuleChainBase::iterator_to(Node &module) { return iterator(&module); }
ModuleChainBase::const_iterator ModuleChainBase::iterator_to(Node const &module)
{
    return const_iterator(&module);
}

ModuleChainBase::iterator ModuleChainBase::begin()
{
    //LE_ASSUME( this->next_.get() );
    //return reinterpret_cast<iterator const &>( this->next_ );
    auto *LE_RESTRICT const pNode(this->next_.get());
    LE_ASSUME(pNode);
    return iterator(pNode);
}
ModuleChainBase::const_iterator ModuleChainBase::begin() const
{
    auto const *LE_RESTRICT const pNode(this->next_.get());
    LE_ASSUME(pNode);
    return const_iterator(pNode);
    //return reinterpret_cast<const_iterator const &>( const_cast<ModuleChainBase &>( *this ).begin() );
}

ModuleChainBase::iterator ModuleChainBase::end() { return this; }
ModuleChainBase::const_iterator ModuleChainBase::end() const { return this; }
bool ModuleChainBase::isEnd(Node const *const pNode) const { return pNode == this; }
void ModuleChainBase::resetRoot()
{
    //...mrmlj...a module sent to be destroyed in the GUI thread might still be
    //...mrmlj...referencing the root node...
    //LE_ASSERT( this->referenceCount_ == 1 || this->referenceCount_ == 3 );
    node_algorithms::init_header(&rootNode());
    //LE_ASSERT( this->referenceCount_ == 3 );
}

bool ModuleChainBase::empty() const { return this->next_.get() == this; }

////////////////////////////////////////////////////////////////////////////////
///
/// \note Saturating rather than truncating, which is what the bare cast was: a
/// chain of 256 counted as **empty**, and every caller of this asks a question
/// where "empty" is an answer it will act on. 255 is a lie too, but it is a lie
/// in the direction of "there are more of these than you expected" rather than
/// "there are none".
///
///   The chain cannot get that long from anything this build does -- the slot
/// model stops at `maxNumberOfModules` and the preset loader refuses the rest --
/// so this is the second guard on a number that has one. It is here because the
/// narrow type is what makes the truncation possible, and the assert is what
/// names it if the bound above is ever the thing that broke.
///
////////////////////////////////////////////////////////////////////////////////

std::uint8_t ModuleChainBase::size() const
{
    auto const modules(node_algorithms::count(&rootNode()) - 1);
    constexpr auto mostThatFits{std::numeric_limits<std::uint8_t>::max()};
    LE_ASSERT_MSG(modules <= mostThatFits, "More modules in the chain than a size can express.");
    return static_cast<std::uint8_t>(std::min<decltype(modules)>(modules, mostThatFits));
}

namespace Detail
{

module_node_traits::node_ptr module_node_traits::get_next(const_node_ptr const n)
{
    return n->next_.get();
}
module_node_traits::node_ptr module_node_traits::get_previous(const_node_ptr const n)
{
    return n->previous_.get();
}

// next and prev can be null here (e.g. when called by the node_algorithms::init() function).
void module_node_traits::set_next(node_ptr const n, node_ptr const next) { n->next_ = next; }
void module_node_traits::set_previous(node_ptr const n, node_ptr const prev)
{
    n->previous_ = prev;
}

//...mrmlj...cannot (fully) use Boost.Intrusive containers yet...
//module_node_value_traits::node_ptr module_node_value_traits::to_node_ptr( value_type & value )
//{
//    return node_ptr( &value );
//}
//
//module_node_value_traits::const_node_ptr module_node_value_traits::to_node_ptr( value_type const & value )
//{
//    return const_node_ptr( &value );
//}
//
//module_node_value_traits::pointer module_node_value_traits::to_value_ptr( node_ptr const & n )
//{
//    //return boost::static_pointer_cast<value_type>( n );
//    return static_cast<value_type *>( n );
//}
//
//module_node_value_traits::const_pointer module_node_value_traits::to_value_ptr( const_node_ptr const & n )
//{
//    //return boost::static_pointer_cast<value_type const>( n );
//    return static_cast<value_type const *>( n );
//}
} // namespace Detail

void ModuleChainImpl::preProcessAll(Parameters::LFOImpl::Timer const &timer,
                                    Setup const &engineSetup)
{
    forEach<Module>([&](Module &module) { module.preProcess(timer, engineSetup); });
}

void ModuleChainImpl::resetAll(Math::Rng &seedSource)
{
    /// \note Seed before reset, not after: an effect's reset() may draw from the
    /// generator it has just been given (Synth's used to, to randomise its
    /// oscillator phases), and a draw from an unseeded stream is the bug this
    /// whole arrangement exists to remove.
    forEach<ModuleDSP>([&seedSource](ModuleDSP &module) {
        module.seedRandomState(seedSource);
        module.reset();
    });
}

namespace
{
ModuleChainImpl::iterator resize(ModuleChainImpl::iterator const pModulesBegin,
                                 ModuleChainImpl::Node const &endNode,
                                 StorageFactors const &factors)
{
    auto pModulePtr(pModulesBegin);
    while (pModulePtr.get() != &endNode)
    {
        auto &module(actualModule<ModuleDSP>(*pModulePtr));
        if (!module.resize(factors))
            break;
        ++pModulePtr;
    }
    return pModulePtr;
}
} // anonymous namespace

bool ModuleChainImpl::resizeAll(Engine::StorageFactors const &newfactors,
                                Engine::StorageFactors const &currentFactors)
{
    iterator const pModulesBegin(this->begin());
    iterator const reachedIterator(resize(pModulesBegin, *end(), newfactors));
    if (isEnd(reachedIterator))
        return true;
    LE_VERIFY(resize(pModulesBegin, *reachedIterator, currentFactors) == reachedIterator);
    return false;
}

} // namespace LE::SW::Engine
