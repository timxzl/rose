#ifndef __ATERM_TERM_HPP__
#define __ATERM_TERM_HPP__
#include <term.h++>

// TODO: We could link against libaterm and use that to create the
//       term representation in memory.

namespace term {

  class ATermTerm : virtual public STLTerm {
   /// Properly quote and escape an atom if necessary
  public:
    static void quote(std::ostream& r, const std::string atom) {
     //std::cerr<<"@@@@"<<std::endl;
     if (atom.length() == 0) {
       r << "\"\"";
     } else if (((atom.length() > 0) && (!islower(atom[0])) && (!isdigit(atom[0])))
		|| needs_quotes(atom)) {
       r << "\"";
       escape(r, atom);
       r << "\"";
     } else {
       escape(r, atom);
     }
   }
  };
  class ATermAtom : virtual public STLAtom {
  public:
    ATermAtom(const std::string name = "#ERROR", bool escapedRepresentation = true) :
      STLAtom(name, escapedRepresentation) { };
    /// return the string
    std::string getRepresentation() const {
      std::ostringstream oss;
      dump(oss);
      return oss.str();
    }
    /// dump term representation to an ostream
    virtual void dump(std::ostream& s) const {
      quote(s, mName);
    }

    /// Properly quote and escape an atom if necessary
    static void quote(std::ostream& r, const std::string atom) {
      if (atom.length() == 0) {
	r << "\"\"";
      } else if (((atom.length() > 0) && (!islower(atom[0])) && (!isdigit(atom[0])))
		|| needs_quotes(atom)) {
	r << "\"";
	escape(r, atom);
	r << "\"";
      } else {
	escape(r, atom);
      }
    }

    // true if the pattern can be unified with the term
    bool matches(std::string pattern) { return false; assert(false && "not implemented"); }

  protected:

    static bool needs_quotes(const std::string s) {
      if (s.length() == 0) 
	return true;

      for (std::string::const_iterator c = s.begin();
	   c != s.end(); ++c) {

	if (!islower(*c) && !isupper(*c) && !(*c == '_'))
	  return true;
      }
      return false;
    }

    // Escape non-printable characters
    static void escape(std::ostream& r, std::string s) {
      for (unsigned int i = 0; i < s.length(); ++i) {
	unsigned char c = s[i];
	switch (c) {
	case '\\': r << "\\\\"; break; // Literal backslash
	case '\"': r << "\\\""; break; // Double quote
	case '\'': r << "\\'"; break;  // Single quote
	case '\n': r << "\\n"; break;  // Newline (line feed)
	case '\r': r << "\\r"; break;  // Carriage return
	case '\b': r << "\\b"; break;  // Backspace
	case '\t': r << "\\t"; break;  // Horizontal tab
	case '\f': r << "\\f"; break;  // Form feed
	case '\a': r << "\\a"; break;  // Alert (bell)
	case '\v': r << "\\v"; break;  // Vertical tab
	case '!' : r << "MINITERMITE-STRATEGO-BANG";	   break;
	case '#' : r << "MINITERMITE-STRATEGO-OCTOTHORPE"; break;
	default:
	  if (c < 32 || c > 127) {
	    r << '\\' 
	      << std::oct 
	      << std::setfill('0') 
	      << std::setw(3) 
	      << (unsigned int)c; // \nnn Character with octal value nnn
	   } else {
	    r << c;
	  }
	}
      }
      //cerr<<"escape("<<s<<") = "<< r <<endl;
    }

  };

  /// Representation of a compound prolog term.
  class ATermCompTerm : public STLCompTerm {
  public:
    virtual void quote1(std::ostream& r, const std::string atom) const {
      // get around diamond inheritance woes
      ATermTerm::quote(r, atom);
    }
    /// Creates a compound term with the given name. no subterms added yet.
    ATermCompTerm(const std::string name = "#ERROR") : STLCompTerm(name) {};

    ATermCompTerm(const std::string name, Term* t1)
      : STLCompTerm(name,t1)
    { }

    ATermCompTerm(const std::string name, Term* t1, Term* t2) 
      : STLCompTerm(name,t1,t2)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3) 
      : STLCompTerm(name,t1,t2,t3)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3, 
		Term* t4) 
      : STLCompTerm(name,t1,t2,t3,t4)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3, 
		Term* t4, Term* t5) 
      : STLCompTerm(name,t1,t2,t3,t4,t5)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3,
		Term* t4, Term* t5, Term* t6) 
      : STLCompTerm(name,t1,t2,t3,t4,t5,t6)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3, 
		Term* t4, Term* t5, Term* t6, 
		Term* t7)
      : STLCompTerm(name,t1,t2,t3,t4,t5,t6,t7)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3, 
		Term* t4, Term* t5, Term* t6, 
		Term* t7, Term* t8)
      : STLCompTerm(name,t1,t2,t3,t4,t5,t6,t7,t8)
    { }

    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3, 
		Term* t4, Term* t5, Term* t6, 
		Term* t7, Term* t8, Term* t9)
      : STLCompTerm(name,t1,t2,t3,t4,t5,t6,t7,t8,t9)
    { }
  
    ATermCompTerm(const std::string name, 
		Term* t1, Term* t2, Term* t3, 
		Term* t4, Term* t5, Term* t6, 
		Term* t7, Term* t8, Term* t9,
		Term* t10)
      : STLCompTerm(name,t1,t2,t3,t4,t5,t6,t7,t8,t9,t10)
    { }
  };

  /**
   * Create terms in ATerm format.
   */
  class ATermTermFactory : public STLTermFactory {
    /// create a new atom
    Atom* makeAtom(const std::string& name, bool escape) const 
    { return new ATermAtom(name, escape); };

    /// create a new int
    Int* makeInt(const int value) const { return new STLInt(value); }

    /// create a new float
    Float* makeFloat(const float value) const { return new STLFloat(value); }

    /// create a new List
    List* makeList() const { return new STLList(); }
    List* makeList(std::deque<term::Term*>& v) const { return new STLList(v); }
    List* makeList(std::vector<term::Term*>& v) const { return new STLList(v); }

    /// create a new compound term
    //  Yes, I do know about variadic functions.
    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1) const
    { return new ATermCompTerm(name, t1); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2) const
    { return new ATermCompTerm(name, t1, t2); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3) const
    { return new ATermCompTerm(name, t1, t2, t3); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4) const
    { return new ATermCompTerm(name, t1, t2, t3, t4); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4, term::Term* t5) const
    { return new ATermCompTerm(name, t1, t2, t3, t4, t5); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4, term::Term* t5, term::Term* t6) const
    { return new ATermCompTerm(name, t1, t2, t3, t4, t5, t6); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4, term::Term* t5, term::Term* t6,
			   term::Term* t7) const
    { return new ATermCompTerm(name, t1, t2, t3, t4, t5, t6, t7); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4, term::Term* t5, term::Term* t6,
			   term::Term* t7, term::Term* t8) const
    { return new ATermCompTerm(name, t1, t2, t3, t4, t5, t6, t7, t8); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4, term::Term* t5, term::Term* t6,
			   term::Term* t7, term::Term* t8, term::Term* t9) const
    { return new ATermCompTerm(name, t1, t2, t3, t4, t5, t6, t7, t8, t9); }

    CompTerm* makeCompTerm(const std::string& name, 
			   term::Term* t1, term::Term* t2, term::Term* t3,
			   term::Term* t4, term::Term* t5, term::Term* t6,
			   term::Term* t7, term::Term* t8, term::Term* t9,
			   term::Term* t10) const
    { return new ATermCompTerm(name, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10); }
  };
}
#endif
