// Stuff that's useful beyond this program

#pragma once
#include <coreinit/fastmutex.h>
#include <type_traits>

namespace cz {
    namespace cos_util {
        class cos_conditional_lock {
            OSMutex *m_mutex;
            bool m_doAcquire;

        public:
            cos_conditional_lock(OSMutex &mutex, bool doAcquire) noexcept
                : m_mutex(&mutex), m_doAcquire(doAcquire) {
                if (m_doAcquire)
                    OSLockMutex(m_mutex);
            }

            ~cos_conditional_lock() noexcept {
                if (m_doAcquire)
                    OSUnlockMutex(m_mutex);
            }
        };
    }

    namespace util {
        void Discard(auto &&) {
            // Does nothing
        }

        namespace ops {
            template<typename T> requires std::is_scoped_enum_v<T>
            std::underlying_type_t<T> operator+(T t) noexcept {
                return static_cast<std::underlying_type_t<T>>(t);
            }
        }
    }
}
