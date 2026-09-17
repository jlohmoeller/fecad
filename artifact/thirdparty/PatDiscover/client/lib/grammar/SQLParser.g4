parser grammar SQLParser;

options { tokenVocab=SQLLexer; }

query
    : START expr SEMI EOF ;

expr : element (AND element)* ;

element : (bool_paren_group | bool_expr)
        | (enum_paren_group | enum_expr)
        | (range_paren_group | range_expr)
        | (distance_paren_group | distance_expr) 
        ;

bool_group_expr : bool_and_group
                | bool_or_group
                | bool_sum_group
                ;

bool_and_group : bool_expr (AND bool_expr)*
               | bool_paren_group (AND bool_paren_group)*
               ;

bool_or_group : bool_expr (OR bool_expr)*
              | bool_paren_group (OR bool_paren_group)*
              ;

bool_sum_group : bool_expr (SUM bool_expr)*
               | bool_paren_group (SUM bool_paren_group)*
               ;

bool_paren_group : LPAREN bool_group_expr RPAREN
                 ;

bool_expr : ID EQ TRUE
          | ID EQ FALSE
          ;

// Enum Data Type
enum_group_expr
    : enum_and_group
    | enum_or_group
    | enum_sum_group
    ;

enum_and_group
    : enum_paren_group (AND enum_paren_group)*
    | enum_expr (AND enum_expr)*
    ;

enum_or_group
    : enum_paren_group (OR enum_paren_group)*
    | enum_expr (OR enum_expr)*
    ;

enum_sum_group
    : enum_paren_group (SUM enum_paren_group)*
    | enum_expr (SUM enum_expr)*
    ;

enum_paren_group
    : LPAREN enum_group_expr RPAREN
    ;

enum_expr
    : ID EQ INT
    | ID NEQ INT
    ;

// Range Data Type
range_group_expr
    : range_and_group
    | range_or_group
    | range_sum_group
    ;

range_and_group
    : range_paren_group (AND range_paren_group)*
    | range_expr (AND range_expr)*
    ;

range_or_group
    : range_paren_group (OR range_paren_group)*
    | range_expr (OR range_expr)*
    ;

range_sum_group
    : range_paren_group (SUM range_paren_group)*
    | range_expr (SUM range_expr)*
    ;

range_paren_group
    : LPAREN range_group_expr RPAREN
    ;

range_expr
    : ID NOT? BETWEEN INT AND INT
    | ID NOT? BETWEEN REAL AND REAL
    ;

// Distance Data Type
distance_group_expr
    : distance_and_group
    | distance_or_group
    | distance_sum_group
    ;

distance_and_group
    : distance_paren_group (AND distance_paren_group)*
    | distance_expr (AND distance_expr)*
    ;

distance_or_group
    : distance_paren_group (OR distance_paren_group)*
    | distance_expr (OR distance_expr)*
    ;

distance_sum_group
    : distance_paren_group (SUM distance_paren_group)*
    | distance_expr (SUM distance_expr)*
    ;

distance_paren_group
    : LPAREN distance_group_expr RPAREN
    ;

distance_expr
    : DISTANCE FROM ID TO point LE REAL
    | DISTANCE FROM ID TO point GT REAL
    ;

point : POINT LPAREN REAL COMMA REAL COMMA REAL RPAREN ;
