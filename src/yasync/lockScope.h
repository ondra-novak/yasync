#pragma once

namespace yasync {



///Locks the current scope (unlocks in destructor)
template<typename _Mutex>
class LockScope {

public:
  typedef _Mutex mutex_type;

  explicit LockScope(mutex_type& __m) : mutex(__m)
  { mutex.lock(); }

  LockScope(mutex_type& __m, bool locked) : mutex(__m)
  { if (!locked) mutex.lock(); }

  ~LockScope()
  { mutex.unlock(); }

  LockScope(const LockScope&) = delete;
  LockScope& operator=(const LockScope&) = delete;

private:
  mutex_type&  mutex;
};

///Unlocks the current scope (locks in destructor)
template<typename _Mutex>
class UnlockScope {

public:
  typedef _Mutex mutex_type;

  explicit UnlockScope(mutex_type& __m) : mutex(__m)
  { mutex.unlock(); }

  ~UnlockScope()
  { mutex.lock(); }

  UnlockScope(const UnlockScope&) = delete;
  UnlockScope& operator=(const UnlockScope&) = delete;

private:
  mutex_type&  mutex;
};

}
