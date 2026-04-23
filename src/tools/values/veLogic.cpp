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
#include "veLogic.h"

using namespace vValue::Registry;

namespace vValue {
namespace Expr {
namespace Logic {

// Helper visitor for converting Variant to bool
struct ToBoolVisitor {
    bool operator()(int i) const { return i != 0; }
    bool operator()(float f) const { return f != 0.0f; }
    bool operator()(const std::string& s) const {
        return !s.empty() && s != "0" && s != "false" && s != "FALSE";
    }
};

Registration register_iff("func\nlogic", "iff", 3, (Registration::fptr)
                          ( ctor::a3* )& Creator<Condition>::create<BasePtr,BasePtr,BasePtr> );

//! Constructs a new Condition object with the given parameters
//! @param condvalue  the value to be used as the condition
//! @param truevalue  the value to be used if the condition is true
//! @param falsevalue the value to be used if the condition is false
Condition::Condition(BasePtr condvalue, BasePtr truevalue, BasePtr falsevalue) :
        m_condvalue (condvalue ),
        m_truevalue (truevalue ),
        m_falsevalue(falsevalue)
{
}

//! Initializes the Condition object using the information from another one
//! @param other another Condition object
Condition::Condition(Condition const &other) :
        Base        (other             ),
        m_condvalue (other.m_condvalue ->copy()),
        m_truevalue (other.m_truevalue ->copy()),
        m_falsevalue(other.m_falsevalue->copy())
{ }

//! Overwritten copy function
//! @returns a new copy of the current object
Base *Condition::copy(void) const {
    return new Condition(*this);
}

//! Uses a specific type (usually int or float) to perform the comparison
//! @param fun the return value of this function is used for the comparison
//! @returns a reference to either m_lvalue or m_rvalue based on if the condition is true or false
Base const &Condition::GetExpr() const {
    bool truth = std::visit(ToBoolVisitor{}, m_condvalue->GetValue());
    return truth ? *m_truevalue : *m_falsevalue;
}

//! Performs the comparison and returns the resulting value
//! @returns the result as a variant
Variant Condition::GetValue(void) const {
    return GetExpr().GetValue();
}

//! Performs the comparison and returns the resulting value
//! @returns the result as an integer
int Condition::GetInt(void) const {
    return GetExpr().GetInt();
}

//! Performs the comparison and returns the resulting value
//! @returns the result as a float
float Condition::GetFloat(void) const {
    return GetExpr().GetFloat();
}

//! Performs the comparison and returns the resulting value
//! @returns the result as a string
tString Condition::GetString(Expr::Base const *other) const {
    return GetExpr().GetString(other);
}

bool Condition::operator==(Base const &other) const { return GetExpr() == other; }
bool Condition::operator!=(Base const &other) const { return GetExpr() != other; }
bool Condition::operator>=(Base const &other) const { return GetExpr() >= other; }
bool Condition::operator<=(Base const &other) const { return GetExpr() <= other; }
bool Condition::operator> (Base const &other) const { return GetExpr() >  other; }
bool Condition::operator< (Base const &other) const { return GetExpr() <  other; }

//! Returns the value negated
//! @returns the result
Variant
Not::GetValue(void) const {
    bool truth = std::visit(ToBoolVisitor{}, m_value->GetValue());
    return (int)(!truth);
}

Base *Not::copy(void) const {
    return new Not(*this);
}

}
}
}
