lexer grammar SQLLexer;

START : 'SELECT id, provider FROM patients WHERE' ;

FROM : 'FROM' ;
TO : 'TO' ;
AND : 'AND' ;
OR : 'OR' ;
SUM : 'SUM' ;
EQ : '=' ;
NEQ : '!=' ;
LE : '<=' ;
GT : '>' ;
BETWEEN : 'BETWEEN' ;
NOT : 'NOT' ;
DISTANCE: 'DISTANCE' ;
POINT: 'POINT' ;
TRUE: 'TRUE' ;
FALSE: 'FALSE' ;

COMMA : ',' ;
SEMI : ';' ;
LPAREN : '(' ;
RPAREN : ')' ;

INT : [0-9]+ ;
REAL : [0-9]+ '.' [0-9]+ ;

ID: [a-zA-Z_][a-zA-Z_0-9]* ;
WS: [ \t\n\r\f]+ -> skip ;