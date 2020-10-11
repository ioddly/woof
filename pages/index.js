import Head from 'next/head'
import styles from '../styles/Home.module.css'

export default function Home() {
  return (
    <div className={styles.container}>
      <Head>
        <title>woof</title>
        <link rel="icon" href="/favicon.ico" />
      </Head>

      <p>hello world 2</p>
    </div>
  )
}
