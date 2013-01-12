 module if_scope

 contains

 subroutine foo

!   integer ::      n

!   n = 0

   if (n > 0) n = 0

 end subroutine foo

 end module if_scope
