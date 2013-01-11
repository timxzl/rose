/* -*- C++ -*-
Copyright 2006 Christoph Bonitz <christoph.bonitz@gmail.com>
          2007-2012 Adrian Prantl <adrian@complang.tuwien.ac.at>
*/
#include "minitermite.h"
#include <iostream>
#include <stdio.h>
#include <rose.h>
#include "TermToRose.h"
#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <rose_config.h>

using namespace std;
using namespace term;

int main(int argc, char** argv) {
    // Process command line options
    po::options_description desc(string("Usage: ")+argv[0]+string(
    " [OPTIONS] file.term\n"
    "  Unparse a term file to its original source representation\n"
    "\n"
    "This program was built against " PACKAGE_STRING ",\n"
    "Please report bugs to <" PACKAGE_BUGREPORT ">.\n\n"
    "Options"));
  desc.add_options()
    ("help,h", "produce this help message")
    ("rose-help", "Display the help for the C/C++/Fortran frontend")
    ("version,v", "Display the version")
    ("input,i",  po::value< string >(), "input file")
    ("output,o", po::value< string >()->default_value(""), 
     "Override the name of the unparsed file.\n"
     "For multi-file projects, this will only affect the first file.")
    ("suffix,s", po::value< string >()->default_value(".unparsed"), 
     "Use the original file names with this additional suffix appended.")
    ("dir,d", po::value< string >()->default_value("."), 
     "Create the unparsed files in DIRECTORY.")
    ("dot", "Create a graphviz graph of the syntax tree.")
    ("pdf", "Create a PDF printout of the syntax tree.")
    ("stratego", "Expect term input in Stratego/XT format.")
    ("aterm",	 "Expect term input in ATerm format.")
    ("stl-engine",
#if ROSE_HAVE_SWI_PROLOG
     "Do not use SWI-Prolog to parse term output."
#else
     "Ignored for compatibility."
#endif
     );
  po::variables_map args;
  po::positional_options_description pos;
  pos.add("input", 1);
  try {
    po::store(po::command_line_parser(argc, argv).
	      options(desc).positional(pos).run(), args);
    po::notify(args);

#if ROSE_HAVE_SWI_PROLOG
    int stl_flag = 0;
#else
    int stl_flag = 1;
#endif
    if (args.count("help")) {
      cout << desc << endl;
      return 0;
    }

    if (!args.count("input")) {
      cout << desc << endl;
      return 1;
    }

    if (args.count("version")) {
      cout<<argv[0]<<", "<<PACKAGE_STRING<<" version "<<PACKAGE_VERSION<<endl; 
      return 0;
    }

    if (args.count("stl-engine")) {
      stl_flag = 1;
    }

    if (args.count("stratego") && args.count("aterm")) {
      cout<<"**ERROR: The --stratego and --aterm options are mutually exclusive!"<<endl;
      exit(1);
    }

    init_termite(argc, argv);

    // Choose the way to parse terms based on the options
    TermFactory* termFactory;
    if (args.count("stratego")) {
      yy_use_stratego_filter = true;
      termFactory = new StrategoTermFactory();
    } else if (args.count("aterm")) {
      yy_use_stratego_filter = true;
      termFactory = new ATermTermFactory();
    } else 
#if ROSE_HAVE_SWI_PROLOG
      if (stl_flag) termFactory = new STLTermFactory();
      else termFactory = new SWIPLTermFactory();
#else
    termFactory = new STLTermFactory();
#endif

    TermToRose conv(*termFactory);
    string fn = args["input"].as<string>();
    SgNode* p = conv.toRose(fn.c_str());

    if (args.count("dot")) {
      //  Create dot and pdf files
      //  DOT generation (numbering:preoder)
      AstDOTGeneration dotgen;
      dotgen.generateInputFiles((SgProject*)p,AstDOTGeneration::PREORDER);
    }
    if (args.count("pdf")) {
      //  PDF generation
      AstPDFGeneration pdfgen;
      pdfgen.generateInputFiles((SgProject*)p);
    }
    conv.unparse(args["output"].as<string>(), 
		 args["dir"].as<string>(), 
		 args["suffix"].as<string>(),
		 p);
  }
  catch(po::error& e) { 
    cerr << "**ERROR: " << e.what() << endl;
    exit(1);
  }
  return 0;
}
