void printf(const char*);
void f() {
  printf("\n\000\r\n");
  printf(""); // \BEL
  printf("0A");
  printf("0\n");
  printf("'");
  { char c = '"'; }
}
