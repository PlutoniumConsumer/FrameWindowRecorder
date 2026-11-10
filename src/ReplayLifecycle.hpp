#pragma once
namespace meter {
// Native restart/setup can dispatch destroyPlayer without a gameplay collision.
// Keep native behavior, but don't interpret those callbacks as trial outcomes.
class ReplayLifecycle {
    unsigned m_resetDepth=0;
public:
    class ResetScope {
        ReplayLifecycle& m_owner;
    public:
        explicit ResetScope(ReplayLifecycle& owner):m_owner(owner){++m_owner.m_resetDepth;}
        ~ResetScope(){--m_owner.m_resetDepth;}
        ResetScope(ResetScope const&)=delete;
        ResetScope& operator=(ResetScope const&)=delete;
    };
    bool resetting() const {return m_resetDepth!=0;}
    bool gameplayDeath(bool syntheticProbe,bool activePlayer) const {
        return !resetting() && !syntheticProbe && activePlayer;
    }
};
}
