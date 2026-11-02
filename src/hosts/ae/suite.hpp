#pragma once

#include "AE_Effect.h"
#include "SPBasic.h"
#include <stdexcept>

namespace functional_fx_suite::hosts::ae
{
    // scoped acquisition of a PICA suite; throws when the host doesn't provide it
    template <class T> class suite
    {
      public:
        suite(const PF_InData* in_data, const char* name, int version)
            : basic_(in_data->pica_basicP), name_(name), version_(version)
        {
            const void* ptr = nullptr;
            if (basic_ == nullptr || basic_->AcquireSuite(name_, version_, &ptr) != kSPNoError || ptr == nullptr)
            {
                throw std::runtime_error("host suite unavailable");
            }
            suite_ = static_cast<const T*>(ptr);
        }
        suite(const suite&) = delete;
        suite& operator=(const suite&) = delete;
        ~suite()
        {
            basic_->ReleaseSuite(name_, version_);
        }

        const T* operator->() const
        {
            return suite_;
        }

      private:
        SPBasicSuite* basic_;
        const char* name_;
        int version_;
        const T* suite_ = nullptr;
    };
} // namespace functional_fx_suite::hosts::ae
