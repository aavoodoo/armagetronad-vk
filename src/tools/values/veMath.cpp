/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

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

#include "vCore.h"
#include "vRegistry.h"
#include "vebCFunction.h"
#include "veMath.h"

long int ve_math_random()
{
    return (long int)tRandomizer::GetInstance().Get(RAND_MAX);
}


using namespace vValue::Registry;

namespace vValue {
namespace Expr {
namespace Math {

//! Returns the result of adding the lvalue and rvalue
//! @returns the result
Variant
Add::GetValue(void) const {
    const Variant lvalue = m_lvalue->GetValue();
    const Variant rvalue = m_rvalue->GetValue();
    //return boost::apply_visitor(AddVisitor(), lvalue, rvalue);
    /*
    	if (std::get_if<std::string>(&lvalue) || std::get_if<std::string>(&rvalue))
    		return static_cast<std::string>lvalue
    		     + static_cast<std::string>rvalue;
    	else
    */
    if (std::get_if<int>(&lvalue) && std::get_if<int>(&rvalue))
                return std::get<int>(lvalue) + std::get<int>(rvalue);
    /*
    else
    if (std::get_if<float>(&lvalue) || std::get_if<float>(&rvalue))
    	return static_cast<float>(lvalue)
    	     + static_cast<float>(rvalue);
    else
    	throw(1);*/
    return m_lvalue->GetFloat() + m_rvalue->GetFloat();
}

Base *Add::copy(void) const {
    return new Add(*this);
}

//! Returns the result of subtracting rvalue from lvalue
//! @returns the result
Variant
Subtract::GetValue(void) const {
    const Variant lvalue = m_lvalue->GetValue();
    const Variant rvalue = m_rvalue->GetValue();
    if (std::get_if<int>(&lvalue) && std::get_if<int>(&rvalue))
                return std::get<int>(lvalue) - std::get<int>(rvalue);
    return m_lvalue->GetFloat() - m_rvalue->GetFloat();
    /*
    if (std::get_if<float>(&lvalue) || std::get_if<float>(&rvalue))
    	return static_cast<float>(lvalue)
    	     - static_cast<float>(rvalue);
    else
    if (std::get_if<int>(&lvalue) && std::get_if<int>(&rvalue))
    	return std::get<int>(lvalue) - std::get<int>(rvalue);
    else
    	throw(1);
    */
}

Base *Subtract::copy(void) const {
    return new Subtract(*this);
}

//! Returns the result of multiplying lvalue by rvalue
//! @returns the result
Variant
Multiply::GetValue(void) const {
    const Variant lvalue = m_lvalue->GetValue();
    const Variant rvalue = m_rvalue->GetValue();
    if (std::get_if<int>(&lvalue) && std::get_if<int>(&rvalue))
                return std::get<int>(lvalue) * std::get<int>(rvalue);
    return m_lvalue->GetFloat() * m_rvalue->GetFloat();
}

Base *Multiply::copy(void) const {
    return new Multiply(*this);
}

//! Returns the result of dividing lvalue by rvalue
//! @returns the result
Variant
Divide::GetValue(void) const {
    // Operate on both values as floats, because this is division
    return m_lvalue->GetFloat() / m_rvalue->GetFloat();
}

Base *Divide::copy(void) const {
    return new Divide(*this);
}

//! Returns the result of lvalue ^ rvalue
//! @returns the result
Variant
Power::GetValue(void) const {
    // Operate on both values as floats, using pow() fron math.h which is #included in veMath.h
    return (float) pow(m_lvalue->GetFloat(),m_rvalue->GetFloat());
}

Base *Power::copy(void) const {
    return new Power(*this);
}

//! Returns the rvalue th root of lvalue
//! @returns the result
Variant
Root::GetValue(void) const {
    // Operate on both values as floats, using pow() fron math.h which is #included in veMath.h
    return (float) pow(m_lvalue->GetFloat(), 1 / m_rvalue->GetFloat());
}

Base *Root::copy(void) const {
    return new Root(*this);
}


Registration register_sin("func\nmath", "sin", 1, (Registration::fptr)
                          ( ctor::a1* )& Creator<Trig::Sin>::create<BasePtr> );


}
}
}
