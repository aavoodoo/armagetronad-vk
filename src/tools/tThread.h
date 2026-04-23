/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2011  Armagetron Advanced Development Team

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#ifndef ArmageTron_THREAD_H
#define ArmageTron_THREAD_H

#include "defs.h"
#include <thread>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

// Provide boost-compatible interface for code that uses boost::thread::attributes
namespace boost
{
class thread
{
public:
    struct attributes{
        size_t stack_size{};

        void set_stack_size(size_t s){stack_size = s;}
    };

    template< class T>
    void launch( attributes const & a, T const & t )
    {
#ifdef HAVE_PTHREAD
        // Use pthreads when we need to set stack size
        if(a.stack_size)
        {
            pthread_t pthread;
            T * o = new T(t);

            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, a.stack_size);

            pthread_create(&pthread, &attr, &run<T>, (void*) o);

            pthread_attr_destroy(&attr);
            return;
        }
#endif
        // Use std::thread when no special attributes needed
        std::thread([t]() mutable { t(); }).detach();
    }

    template< class T>
    thread( attributes const & a, T const & t )
    {
        launch(a, t);
    }

    template< class T>
    thread( T const & t )
    {
        launch(attributes{}, t);
    }

    void detach(){}

private:
#ifdef HAVE_PTHREAD
    // worker function for pthread
    template< class T >
    static void * run( void * o )
    {
        T * t = static_cast< T * >(o);

        // do the actual call
        (*t)();

        // clean up
        delete t;

        return NULL;
    }
#endif
};
}

#endif
