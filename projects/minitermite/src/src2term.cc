/* -*- C++ -*-
Copyright 2006 Christoph Bonitz <christoph.bonitz@gmail.com>
          2007-2012 Adrian Prantl <adrian@complang.tuwien.ac.at>

 * Purpose: create a TERMITE representation of a given AST
 */

#include <iostream>
#include <fstream>

#include <rose.h>
#include <rose_config.h>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

// GB (2009-02-25): We want to build c2term without ICFG stuff to avoid
// having to link against libsatire. Also, want to avoid PAG DFI stuff.
#define HAVE_SATIRE_ICFG 0
#undef HAVE_PAG
#include "TermPrinter.h"

using namespace std;
using namespace term;

int main(int argc, char** argv) {
  // Turn off the frontend's warnings; they are distracting in the
  // automated test outputs.
  vector<char*> argv1;
  char warningOpt[] = "-edg:w";
  char includeOpt[] = "-I" ROSE_INCLUDE_DIR;
  argv1.push_back(argv[0]);
  argv1.push_back(warningOpt);
  argv1.push_back(includeOpt);
  for (int i = 1; i < argc; ++i)
    argv1.push_back(argv[i]);
  int argc1 = argc+2;

  // Process our own options
  po::options_description desc(string("Usage: ")+argv[0]+string(
    " [FRONTEND OPTIONS] [--dot] [--pdf] src1.c src2.cpp src3.f ... [-o file.term]\n"
    "  Parse one or more source files and serialize them into TERMITE terms.\n"
    "  Header files will be included in the term representation.\n"
    "\n"
    "This program was built against " PACKAGE_STRING ",\n"
    "Please report bugs to <" PACKAGE_BUGREPORT ">.\n\n"
     "Options (additional options will be passed to the C/C++/Fortran frontend)"));

  po::arg="file.term";
  desc.add_options()
    ("help,h", "produce this help message")
    ("rose-help", "Display the help for the C/C++/Fortran frontend")
    ("version,v", "Display the version")
    ("output,o", po::value< string >(), "Write the output to <file.term> instead of stdout.")
    ("dot", "Create a graphviz graph of the syntax tree.")
    ("pdf", "Create a PDF printout of the syntax tree.")
    ("stratego", "Create term output compatible with the Stratego/XT term rewrite system.")
    ("aterm", "Create term output in ATerm syntax.")
    ("stl-engine",
#if ROSE_HAVE_SWI_PROLOG
     "Do not use SWI-Prolog to generate term output."
#else
     "Ignored for compatibility."
#endif
     );
  po::variables_map args;
  try {
    po::store(po::command_line_parser(argc, argv).
	      options(desc).allow_unregistered().run(), args);
    po::notify(args);

#if ROSE_HAVE_SWI_PROLOG
    int stl_flag = 0;
#else
    int stl_flag = 1;
#endif

    if (argc <= 1 || args.count("help")) {
      cout << desc << "\n";
      return 0;
    }

    if (args.count("rose-help")) {
      argc1 = 2;
      argv1[1] = strdup("--help");
      frontend(argc1,&argv1[0]);
      return 0;
    }
    
    if (args.count("version")) {
      cout<<argv[0]<<", "<<PACKAGE_STRING<<" version "<<PACKAGE_VERSION<<endl; 
      return 0;
    }
    
    //cerr<<"% frontend"<<endl;

    // Run the EDG frontend
    SgProject* project = frontend(argc1,&argv1[0]);

    if (args.count("dot")) {
      //  DOT generation (numbering:preoder)
      AstDOTGeneration dotgen;
      dotgen.generateInputFiles(project, AstDOTGeneration::PREORDER);
    }
    if (args.count("pdf")) {
      //  PDF generation
      AstPDFGeneration pdfgen;
      pdfgen.generateInputFiles(project);
    }

    if (args.count("stl-engine")) {
      stl_flag = 1;
    }

    init_termite(argc, argv);

    // Choose the way to construct terms based on the options
    TermFactory* termFactory;
    if (args.count("stratego") && args.count("aterm")) {
      cout<<"**ERROR: The --stratego and --aterm options are mutually exclusive!"<<endl;
      exit(1);
    }
    bool print_dot = true;
    if (args.count("stratego"))
      termFactory = new StrategoTermFactory();
    else if (args.count("aterm")) {
      termFactory = new ATermTermFactory();
      print_dot = false;
    } else
#if ROSE_HAVE_SWI_PROLOG
      if (stl_flag) termFactory = new STLTermFactory();
      else termFactory = new SWIPLTermFactory();
#else 
      termFactory = new STLTermFactory();
#endif

    // Create prolog term
    BasicTermPrinter tp(*termFactory);
    //cerr<<"% conversion"<<endl;
    tp.traverse(project); // With headers
      
    Term* genTerm = tp.getTerm();
  
    if (args.count("output")) {
      ofstream ofile(args["output"].as<string>().c_str());
      //ofile << genTerm->getRepresentation() << "." << endl;
      //cerr<<"% dump"<<endl;
      genTerm->dump(ofile);
      if (print_dot) ofile << ".";
      ofile << endl;
      ofile.close();
    } else {
      cout << genTerm->getRepresentation();
      if (print_dot) cout << ".";
      cout << endl;
    }
  }
  catch(po::error& e) { 
    cerr << "**ERROR: " << e.what() << endl;
    exit(1);
  }
  return 0;
}
