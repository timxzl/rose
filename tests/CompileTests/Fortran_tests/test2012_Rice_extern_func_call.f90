 module ext_func

  integer, external :: n
            
contains

  function foo(ncid)
    foo = n(ncid)
  end function foo
  
end module
