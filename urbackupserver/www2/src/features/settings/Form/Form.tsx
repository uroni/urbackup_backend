import styles from "./Form.module.css";

export function Form(props: React.ComponentProps<"form">) {
  return <form {...props} className={`${styles.form} ${props.className}`} />;
}

export function FormContainer({ children }: { children: React.ReactNode }) {
  return <div className={`${styles.container} flow`}>{children}</div>;
}

export function FormCard({
  as,
  children,
}: {
  as?: React.ElementType;
  children: React.ReactNode;
}) {
  const Wrapper = as ?? "section";

  return <Wrapper className={styles.card}>{children}</Wrapper>;
}
