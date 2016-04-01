/* lock_files.hpp
Compatibility read-write lock
(C) 2016 Niall Douglas http://www.nedprod.com/
File Created: April 2016


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifndef BOOST_AFIO_SHARED_FS_MUTEX_LOCK_FILES_HPP
#define BOOST_AFIO_SHARED_FS_MUTEX_LOCK_FILES_HPP

#include "../../file_handle.hpp"
#include "base.hpp"

BOOST_AFIO_V2_NAMESPACE_BEGIN

namespace algorithm
{
  namespace shared_fs_mutex
  {
    /*! \class lock_files
    \brief Many entity exclusive compatibility file system based lock

    This is a very simple many entity shared mutex likely to work almost anywhere without surprises.
    It works by trying to exclusively create a file called after the entity's name. If it fails to
    exclusively create any file, it backs out all preceding locks, randomises the order
    and tries locking them again until success. The only real reason to use this implementation
    is its excellent compatibility with almost everything.

    - Compatible with networked file systems.
    - Exponential complexity to number of entities being concurrently locked.

    Caveats:
    - No ability to sleep until a lock becomes free, so CPUs are spun at 100%.
    - Sudden process exit with locks held will deadlock all other users for one minute.
    - Sudden power loss during use will deadlock first user for up to one minute.
    - Cannot hold lock for more than one minute, else other waiters will assume your
    process has crashed and force delete your lock files.
    */
    class lock_files : public shared_fs_mutex
    {
      file_handle::path_type _path;
      std::vector<file_handle> _hs;

      lock_files(file_handle::path_type &&o)
          : _path(std::move(o))
      {
      }
      lock_files(const lock_files &) = delete;
      lock_files &operator=(const lock_files &) = delete;

    public:
      //! The type of an entity id
      using entity_type = shared_fs_mutex::entity_type;
      //! The type of a sequence of entities
      using entities_type = shared_fs_mutex::entities_type;

      //! Move constructor
      lock_files(lock_files &&o) noexcept : _path(std::move(o._path)), _hs(std::move(o._hs)) {}
      //! Move assign
      lock_files &operator=(lock_files &&o) noexcept
      {
        _path = std::move(o._path);
        _hs = std::move(o._hs);
        return *this;
      }

      //! Initialises a shared filing system mutex using the directory at \em lockdir
      //[[bindlib::make_free]]
      static result<lock_files> fs_mutex_byte_ranges(file_handle::path_type lockdir) noexcept { return lock_files(std::move(lockdir)); }

      //! Return the path to the directory being used for this lock
      const file_handle::path_type &path() const noexcept { return _path; }

    protected:
      virtual result<void> _lock(entities_guard &out, deadline d) noexcept override final
      {
        stl11::chrono::steady_clock::time_point began_steady;
        stl11::chrono::system_clock::time_point end_utc;
        if(d)
        {
          if((d).steady)
            began_steady = stl11::chrono::steady_clock::now();
          else
            end_utc = (d).to_time_point();
        }
        size_t n;
        do
        {
          {
            auto undo = detail::Undoer([&] {
              for(; n != (size_t) -1; n--)
              {
                _h.unlock(out.entities[n].value, 1);
              }
            });
            BOOST_OUTCOME_FILTER_ERROR(ret, file_handle::file(std::move(lockfile), file_handle::mode::write, file_handle::creation::if_needed, file_handle::caching::temporary));
            for(n = 0; n < out.entities.size(); n++)
            {
              if(d)
              {
                if((d).steady)
                {
                  if(stl11::chrono::steady_clock::now() >= (began_steady + stl11::chrono::nanoseconds((d).nsecs)))
                    return make_errored_result<void>(ETIMEDOUT);
                }
                else
                {
                  if(stl11::chrono::system_clock::now() >= end_utc)
                    return make_errored_result<void>(ETIMEDOUT);
                }
              }
              deadline nd(std::chrono::seconds(0));
              BOOST_OUTCOME_FILTER_ERROR(guard, _h.lock(out.entities[n].value, 1, out.entities[n].exclusive, nd));
              if(!guard)
                goto failed;
            }
            undo.dismiss();
            continue;
          }
        failed:
          // Randomise out.entities
          std::random_shuffle(out.entities.begin(), out.entities.end());
          // Sleep for a very short time
          std::this_thread::yield();
        } while(n < out.entities.size());
        return make_result<void>();
      }

    public:
      virtual void unlock(entities_type entities) noexcept override final
      {
        for(const auto &i : entities)
        {
          _h.unlock(i.value, 1);
        }
      }
    };

  }  // namespace
}  // namespace

BOOST_AFIO_V2_NAMESPACE_END


#endif
