-- Q3: Combined (7 attributes) — HCC risk profile
-- Male, age>=50, elevated ALT (>30 U/L -> q=300), elevated AST (>20 U/L -> q=200),
-- BMI>=25 (-> q=250), with cirrhosis and type-2 diabetes
SELECT * FROM data WHERE sex = 1 AND age >= 50 AND alt_q >= 300 AND ast_q >= 200 AND bmi_q >= 250 AND icd_cirrhosis = 1 AND icd_diabetes = 1
